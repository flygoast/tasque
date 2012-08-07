#ifndef __CONN_H_INCLUDED__
#define __CONN_H_INCLUDED__

#include <netinet/in.h>
#include "event.h"
#include "tube.h"
#include "job.h"

#define LINE_BUF_SIZE   208 

/* CONN_TYPE_* are bit masks */
#define CONN_TYPE_PRODUCER  0x1
#define CONN_TYPE_WORKER    0x2
#define CONN_TYPE_WAITING   0x4

#define TOTAL_OPS               24

typedef struct conn_st conn_t;

struct conn_st {
    evtent_t    sock;
    char        remote_ip[INET_ADDRSTRLEN];
    int         remote_port;
    char        state;
    char        type;
    conn_t      *next;          /* XXX */
    tube_t      *use;
    int64_t     tickat;         /* time at which to do more work */
    int         tickpos;        /* position in srv->conns */
    job_t       *soonest_job;    /* memorization of the soonest job */
    int         ev;             /* event type: EVENT_RD|WR|HUP */
    int         pending_timeout;    /* seconds */

    char        cmd[LINE_BUF_SIZE]; /* the string is NOT NUL-terminated */
    int         cmd_len;
    int         cmd_read;

    char        *reply;
    int         reply_len;
    int         reply_sent;
    char        reply_buf[LINE_BUF_SIZE]; /* the string is NUL-terminated */

    /* How many bytes of in_job->body have been read so far. If in_job is
     * NULL while in_job_read is nonzero, we are in bit bucket mode and
     * in_job_read's meaning in inverted -- then it counts the bytes that
     * remain to be thrown away. */
    int         in_job_read;
    job_t       *in_job;    /* a job to be read from the client */
    job_t       *out_job;
    int         out_job_sent;
    set_t       watch;
    dlist       reserved_jobs;
};

int conn_less(void *conn_a, void *conn_b);
void conn_record(void *conn, int pos);
void conn_tick(void *tickarg, int ev);
void conn_accept(void *sock, int ev);
void conn_close(conn_t *c);
void conn_set_producer(conn_t *c);
void conn_set_worker(conn_t *c);
job_t *conn_soonest_reserved_job(conn_t *c);
int conn_deadline_soon(conn_t *c);
int conn_ready(conn_t *c);
int64_t conn_tickat(conn_t *c);

#endif /*  __CONN_H_INCLUDED__ */
