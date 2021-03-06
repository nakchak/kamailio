/**
 * Copyright (C) 2016 Daniel-Constantin Mierla (asipto.com)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*!
 * \file
 * \brief Kamailio topoh ::
 * \ingroup topoh
 * Module: \ref topoh
 */

#ifndef _TOPOS_MSG_H_
#define _TOPOS_MSG_H_

#include "../../parser/msg_parser.h"

int tps_update_hdr_replaces(sip_msg_t *msg);
char* tps_msg_update(sip_msg_t *msg, unsigned int *olen);
int tps_route_direction(sip_msg_t *msg);
int tps_skip_msg(sip_msg_t *msg);

int tps_request_received(sip_msg_t *msg, int dialog, int direction);
int tps_response_received(sip_msg_t *msg);
int tps_request_sent(sip_msg_t *msg, int dialog, int direction, int local);
int tps_response_sent(sip_msg_t *msg);
#endif
