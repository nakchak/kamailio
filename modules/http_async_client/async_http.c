/**
 * Copyright 2016 (C) Federico Cabiddu <federico.cabiddu@gmail.com>
 * Copyright 2016 (C) Giacomo Vacca <giacomo.vacca@gmail.com>
 * Copyright 2016 (C) Orange - Camille Oudot <camille.oudot@orange.com>
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <event2/event.h>

#include "../../sr_module.h"
#include "../../dprint.h"
#include "../../ut.h"
#include "../../cfg/cfg_struct.h"
#include "../../lib/kcore/faked_msg.h"
#include "../../modules/tm/tm_load.h"

#include "async_http.h"

/* tm */
extern struct tm_binds tmb;

struct sip_msg *ah_reply = NULL;
str ah_error = {NULL, 0};

async_http_worker_t *workers;
int num_workers = 1;

struct query_params ah_params;

int async_http_init_worker(int prank, async_http_worker_t* worker)
{
	LM_DBG("initializing worker process: %d\n", prank);
	worker->evbase = event_base_new();
	LM_DBG("base event %p created\n", worker->evbase);

	worker->g = shm_malloc(sizeof(struct http_m_global));
	memset(worker->g, 0, sizeof(http_m_global_t));
	LM_DBG("initialized global struct %p\n", worker->g);

	init_socket(worker);

	LM_INFO("started worker process: %d\n", prank);

	return 0;
}

void async_http_run_worker(async_http_worker_t* worker)
{
	init_http_multi(worker->evbase, worker->g);
	event_base_dispatch(worker->evbase);
}

int async_http_init_sockets(async_http_worker_t *worker)
{
	if (socketpair(PF_UNIX, SOCK_DGRAM, 0, worker->notication_socket) < 0) {
		LM_ERR("opening tasks dgram socket pair\n");
		return -1;
	}
	LM_INFO("inter-process event notification sockets initialized\n");
	return 0;
}

void async_http_cb(struct http_m_reply *reply, void *param)
{
	async_query_t *aq;
	cfg_action_t *act;
	unsigned int tindex;
	unsigned int tlabel;
	struct cell *t = NULL;
	sip_msg_t *fmsg;

	if (reply->result != NULL) {
		LM_DBG("query result = %.*s [%d]\n", reply->result->len, reply->result->s, reply->result->len);
	}

	/* clean process-local result variables */
	ah_error.s = NULL;
	ah_error.len = 0;
	memset(ah_reply, 0, sizeof(struct sip_msg));

	/* set process-local result variables */
	if (reply->result == NULL) {
		/* error */
		ah_error.s = reply->error;
		ah_error.len = strlen(ah_error.s);
	} else {
		/* success */
		ah_reply->buf = reply->result->s;
		ah_reply->len = reply->result->len;

		if (parse_msg(reply->result->s, reply->result->len, ah_reply) != 0) {
			LM_DBG("failed to parse the http_reply\n");
		} else {
			LM_DBG("successfully parsed http reply %p\n", ah_reply);
		}
	}

	aq = param;
	act = (cfg_action_t*)aq->param;
	if (aq->query_params.suspend_transaction) {
		tindex = aq->tindex;
		tlabel = aq->tlabel;

		if (tmb.t_lookup_ident(&t, tindex, tlabel) < 0) {
			LM_ERR("transaction not found %d:%d\n", tindex, tlabel);
			LM_DBG("freeing query %p\n", aq);
			free_async_query(aq);
			return;
		}
		// we bring the list of AVPs of the transaction to the current context
		set_avp_list(AVP_TRACK_FROM | AVP_CLASS_URI, &t->uri_avps_from);
		set_avp_list(AVP_TRACK_TO | AVP_CLASS_URI, &t->uri_avps_to);
		set_avp_list(AVP_TRACK_FROM | AVP_CLASS_USER, &t->user_avps_from);
		set_avp_list(AVP_TRACK_TO | AVP_CLASS_USER, &t->user_avps_to);
		set_avp_list(AVP_TRACK_FROM | AVP_CLASS_DOMAIN, &t->domain_avps_from);
		set_avp_list(AVP_TRACK_TO | AVP_CLASS_DOMAIN, &t->domain_avps_to);

		if (t)
			tmb.unref_cell(t);

		LM_DBG("resuming transaction (%d:%d)\n", tindex, tlabel);

		if(act!=NULL)
			tmb.t_continue(tindex, tlabel, act);
	} else {
		fmsg = faked_msg_next();
		if (run_top_route(act, fmsg, 0)<0)
			LM_ERR("failure inside run_top_route\n");
	}

	free_sip_msg(ah_reply);
	free_async_query(aq);

	return;
}

void notification_socket_cb(int fd, short event, void *arg)
{
	(void)fd; /* unused */
	(void)event; /* unused */
	const async_http_worker_t *worker = (async_http_worker_t *) arg;

	int received;
	int i;
	async_query_t *aq;

	http_m_params_t query_params;

	str query;
	str post;

	if ((received = recvfrom(worker->notication_socket[0],
			&aq, sizeof(async_query_t*),
			0, NULL, 0)) < 0) {
		LM_ERR("failed to read from socket (%d: %s)\n", errno, strerror(errno));
		return;
	}

	if(received != sizeof(async_query_t*)) {
		LM_ERR("invalid query size %d\n", received);
		return;
	}

	query = ((str)aq->query);
	post = ((str)aq->post);

	query_params.timeout = aq->query_params.timeout;
	query_params.verify_peer = aq->query_params.verify_peer;
	query_params.verify_host = aq->query_params.verify_host;
	query_params.headers = NULL;
	for (i = 0 ; i < aq->query_params.headers.len ; i++) {
		query_params.headers = curl_slist_append(query_params.headers, aq->query_params.headers.t[i]);
	}
	query_params.method  = aq->query_params.method;

	query_params.ssl_cert.s = NULL;
	query_params.ssl_cert.len = 0;
	if (aq->query_params.ssl_cert.s && aq->query_params.ssl_cert.len > 0) {
		if (shm_str_dup(&query_params.ssl_cert, &(aq->query_params.ssl_cert)) < 0) {
			LM_ERR("Error allocating query_params.ssl_cert\n");
			return;
		}
	}

	query_params.ssl_key.s = NULL;
	query_params.ssl_key.len = 0;
	if (aq->query_params.ssl_key.s && aq->query_params.ssl_key.len > 0) {
		if (shm_str_dup(&query_params.ssl_key, &(aq->query_params.ssl_key)) < 0) {
			LM_ERR("Error allocating query_params.ssl_key\n");
			return;
		}
	}

	query_params.ca_path.s = NULL;
	query_params.ca_path.len = 0;
	if (aq->query_params.ca_path.s && aq->query_params.ca_path.len > 0) {
		if (shm_str_dup(&query_params.ca_path, &(aq->query_params.ca_path)) < 0) {
			LM_ERR("Error allocating query_params.ca_path\n");
			return;
		}
	}

	LM_DBG("query received: [%.*s] (%p)\n", query.len, query.s, aq);

	if (new_request(&query, &post, &query_params, async_http_cb, aq) < 0) {
		LM_ERR("Cannot create request for %.*s\n", query.len, query.s);
		free_async_query(aq);
	}

	if (query_params.ssl_cert.s && query_params.ssl_cert.len > 0) {
		shm_free(query_params.ssl_cert.s);
		query_params.ssl_cert.s = NULL;
		query_params.ssl_cert.len = 0;
	}
	if (query_params.ssl_key.s && query_params.ssl_key.len > 0) {
		shm_free(query_params.ssl_key.s);
		query_params.ssl_key.s = NULL;
		query_params.ssl_key.len = 0;
	}
	if (query_params.ca_path.s && query_params.ca_path.len > 0) {
		shm_free(query_params.ca_path.s);
		query_params.ca_path.s = NULL;
		query_params.ca_path.len = 0;
	}

	return;
}

int init_socket(async_http_worker_t *worker)
{
	worker->socket_event = event_new(worker->evbase, worker->notication_socket[0], EV_READ|EV_PERSIST, notification_socket_cb, worker);
	event_add(worker->socket_event, NULL);
	return (0);
}

int async_send_query(sip_msg_t *msg, str *query, str *post, cfg_action_t *act)
{
	async_query_t *aq;
	unsigned int tindex = 0;
	unsigned int tlabel = 0;
	short suspend = 0;
	int dsize;
	tm_cell_t *t = 0;

	if(query==0) {
		LM_ERR("invalid parameters\n");
		return -1;
	}

	t = tmb.t_gett();
	if (t==NULL || t==T_UNDEFINED) {
		LM_DBG("no pre-existing transaction, switching to transaction-less behavior\n");
	} else if (!ah_params.suspend_transaction) {
		LM_DBG("transaction won't be suspended\n");
	} else {
		if(tmb.t_suspend==NULL) {
			LM_ERR("http async query is disabled - tm module not loaded\n");
			return -1;
		}

		if(tmb.t_suspend(msg, &tindex, &tlabel)<0) {
			LM_ERR("failed to suspend request processing\n");
			return -1;
		}

		suspend = 1;

		LM_DBG("transaction suspended [%u:%u]\n", tindex, tlabel);
	}
	dsize = sizeof(async_query_t);
	aq = (async_query_t*)shm_malloc(dsize);

	if(aq==NULL)
	{
		LM_ERR("no more shm\n");
		goto error;
	}
	memset(aq,0,dsize);

    if(shm_str_dup(&aq->query, query)<0) {
		goto error;
	}

	if (post != NULL) {

		if(shm_str_dup(&aq->post, post)<0) {
			goto error;
		}
	}

	aq->param = act;
	aq->tindex = tindex;
	aq->tlabel = tlabel;
	
	aq->query_params.verify_peer = ah_params.verify_peer;
	aq->query_params.verify_host = ah_params.verify_host;
	aq->query_params.suspend_transaction = suspend;
	aq->query_params.timeout = ah_params.timeout;
	aq->query_params.headers = ah_params.headers;
	aq->query_params.method = ah_params.method;

	aq->query_params.ssl_cert.s = NULL;
	aq->query_params.ssl_cert.len = 0;
	if (ah_params.ssl_cert.s && ah_params.ssl_cert.len > 0) {
		if (shm_str_dup(&aq->query_params.ssl_cert, &(ah_params.ssl_cert)) < 0) {
			LM_ERR("Error allocating aq->query_params.ssl_cert\n");
			goto error;
		}
	}

	aq->query_params.ssl_key.s = NULL;
	aq->query_params.ssl_key.len = 0;
	if (ah_params.ssl_key.s && ah_params.ssl_key.len > 0) {
		if (shm_str_dup(&aq->query_params.ssl_key, &(ah_params.ssl_key)) < 0) {
			LM_ERR("Error allocating aq->query_params.ssl_key\n");
			goto error;
		}
	}

	aq->query_params.ca_path.s = NULL;
	aq->query_params.ca_path.len = 0;
	if (ah_params.ca_path.s && ah_params.ca_path.len > 0) {
		if (shm_str_dup(&aq->query_params.ca_path, &(ah_params.ca_path)) < 0) {
			LM_ERR("Error allocating aq->query_params.ca_path\n");
			goto error;
		}
	}

	set_query_params(&ah_params);

	if(async_push_query(aq)<0) {
		LM_ERR("failed to relay query: %.*s\n", query->len, query->s);
		goto error;
	}

	if (suspend) 
		/* force exit in config */
		return 0;
	
	/* continue route processing */
	return 1;

error:

	if (suspend)
		tmb.t_cancel_suspend(tindex, tlabel);
	free_async_query(aq);
	return -1;
}

int async_push_query(async_query_t *aq)
{
	int len;
	int worker;
	static unsigned long rr = 0; /* round robin */

	str query;

	query = ((str)aq->query);

	worker = rr++ % num_workers;
	len = write(workers[worker].notication_socket[1], &aq, sizeof(async_query_t*));
	if(len<=0) {
		LM_ERR("failed to pass the query to async workers\n");
		return -1;
	}
	LM_DBG("query sent [%.*s] (%p) to worker %d\n", query.len, query.s, aq, worker + 1);
	return 0;
}

void init_query_params(struct query_params *p) {
	memset(&ah_params, 0, sizeof(struct query_params));
	set_query_params(p);
}

void set_query_params(struct query_params *p) {
	p->headers.len = 0;
	p->headers.t = NULL;
	p->verify_host = verify_host;
	p->verify_peer = verify_peer;
	p->suspend_transaction = 1;
	p->timeout = http_timeout;
	p->method = AH_METH_DEFAULT;

	if (p->ssl_cert.s && p->ssl_cert.len > 0) {
		shm_free(p->ssl_cert.s);
		p->ssl_cert.s = NULL;
		p->ssl_cert.len = 0;
	}
	if (ssl_cert.s && ssl_cert.len > 0) {
		if (shm_str_dup(&p->ssl_cert, &ssl_cert) < 0) {
			LM_ERR("Error allocating ssl_cert\n");
			return;
		}
	}

	if (p->ssl_key.s && p->ssl_key.len > 0) {
		shm_free(p->ssl_key.s);
		p->ssl_key.s = NULL;
		p->ssl_key.len = 0;
	}
	if (ssl_key.s && ssl_key.len > 0) {
		if (shm_str_dup(&p->ssl_key, &ssl_key) < 0) {
			LM_ERR("Error allocating ssl_key\n");
			return;
		}
	}

	if (p->ca_path.s && p->ca_path.len > 0) {
		shm_free(p->ca_path.s);
		p->ca_path.s = NULL;
		p->ca_path.len = 0;
	}
	if (ca_path.s && ca_path.len > 0) {
		if (shm_str_dup(&p->ca_path, &ca_path) < 0) {
			LM_ERR("Error allocating ca_path\n");
			return;
		}
	}
}
