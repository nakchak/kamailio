CREATE TABLE topos_d (
    id NUMBER(10) PRIMARY KEY,
    rectime DATE,
    a_callid VARCHAR2(255) DEFAULT '',
    a_uuid VARCHAR2(255) DEFAULT '',
    b_uuid VARCHAR2(255) DEFAULT '',
    a_contact VARCHAR2(128) DEFAULT '',
    b_contact VARCHAR2(128) DEFAULT '',
    as_contact VARCHAR2(128) DEFAULT '',
    bs_contact VARCHAR2(128) DEFAULT '',
    a_tag VARCHAR2(64) DEFAULT '',
    b_tag VARCHAR2(64) DEFAULT '',
    a_rr CLOB DEFAULT '',
    b_rr CLOB DEFAULT '',
    iflags NUMBER(10) DEFAULT 0 NOT NULL,
    a_uri VARCHAR2(128) DEFAULT '',
    b_uri VARCHAR2(128) DEFAULT '',
    r_uri VARCHAR2(128) DEFAULT '',
    a_srcip VARCHAR2(50) DEFAULT '',
    b_srcip VARCHAR2(50) DEFAULT ''
);

CREATE OR REPLACE TRIGGER topos_d_tr
before insert on topos_d FOR EACH ROW
BEGIN
  auto_id(:NEW.id);
END topos_d_tr;
/
BEGIN map2users('topos_d'); END;
/
CREATE INDEX topos_d_rectime_idx  ON topos_d (rectime);
CREATE INDEX topos_d_a_callid_idx  ON topos_d (a_callid);

INSERT INTO version (table_name, table_version) values ('topos_d','1');

CREATE TABLE topos_t (
    id NUMBER(10) PRIMARY KEY,
    rectime DATE,
    a_callid VARCHAR2(255) DEFAULT '',
    a_uuid VARCHAR2(255) DEFAULT '',
    b_uuid VARCHAR2(255) DEFAULT '',
    direction NUMBER(10) DEFAULT 0 NOT NULL,
    x_via CLOB DEFAULT '',
    x_tag VARCHAR2(64) DEFAULT ''
);

CREATE OR REPLACE TRIGGER topos_t_tr
before insert on topos_t FOR EACH ROW
BEGIN
  auto_id(:NEW.id);
END topos_t_tr;
/
BEGIN map2users('topos_t'); END;
/
CREATE INDEX topos_t_rectime_idx  ON topos_t (rectime);
CREATE INDEX topos_t_a_callid_idx  ON topos_t (a_callid);

INSERT INTO version (table_name, table_version) values ('topos_t','1');

