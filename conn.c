#include <stdlib.h>
#include <string.h>
#include <erron.h>
#include <assert.h>
#include <limits.h>
#include "times.h"
#include "dlist.h"
#include "event.h"
#include "tube.h"
#include "job.h"
#include "srv.h"

#define INIT_WATCH_NUM  8
#define min(a, b)       ((a) < (b) ? (a) : (b))

#define STATE_WANTCOMMAND       0
#define STATE_WANTDATA          1
#define STATE_SENDJOB           2
#define STATE_SENDWORD          3
#define STATE_WAIT              4
#define STATE_BITBUCKET         5

/* job body cannot beyond this limit of length */
#define JOB_DATA_LIMIT     (1 << 16 - 1)

#define NAME_CHARS  \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"    \
    "abcdefghijklmnopqrstuvwxyz"    \
    "0123456789-+/;.$_()"

#define CMD_PUT                 "put "
#define CMD_PEEKJOB             "peek "
#define CMD_PEEK_READY          "peek-ready"
#define CMD_PEEK_DELAYED        "peek-delayed"
#define CMD_PEEK_BURIED         "peek-buried"
#define CMD_RESERVE             "reserve"
#define CMD_RESERVE_TIMEOUT     "reserve-with-timeout "
#define CMD_DELETE              "delete "
#define CMD_RELEASE             "release "
#define CMD_BURY                "bury "
#define CMD_KICK                "kick "
#define CMD_TOUCH               "touch "
#define CMD_STATS               "stats"
#define CMD_JOBSTATS            "stats-job "
#define CMD_USE                 "use "
#define CMD_WATCH               "watch "
#define CMD_IGNORE              "ignore "
#define CMD_LIST_TUBES          "list-tubes"
#define CMD_LIST_TUBE_USED      "list-tube-used"
#define CMD_LIST_TUBES_WATCHED  "list-tubes-watched"
#define CMD_STATS_TUBE          "stats-tube "
#define CMD_QUIT                "quit"
#define CMD_PAUSE_TUBE          "pause-tube"

#define CONSTSTRLEN(m)  (sizeof(m) - 1)

#define CMD_PEEK_READY_LEN          CONSTSTRLEN(CMD_PEEK_READY)
#define CMD_PEEK_DELAYED_LEN        CONSTSTRLEN(CMD_PEEK_DELAYED)
#define CMD_PEEK_BURIED_LEN         CONSTSTRLEN(CMD_PEEK_BURIED)
#define CMD_PEEKJOB_LEN             CONSTSTRLEN(CMD_PEEKJOB)
#define CMD_RESERVE_LEN             CONSTSTRLEN(CMD_RESERVE)
#define CMD_RESERVER_TIMEOUT_LEN    CONSTSTRLEN(CMD_RESERVE_TIMEOUT)
#define CMD_DELETE_LEN              CONSTSTRLEN(CMD_DELETE)
#define CMD_BURY_LEN                CONSTSTRLEN(CMD_BURY)
#define CMD_KICK_LEN                CONSTSTRLEN(CMD_KICK)
#define CMD_TOUCH_LEN               CONSTSTRLEN(CMD_TOUCH)
#define CMD_STATS_LEN               CONSTSTRLEN(CMD_STATS)
#define CMD_JOBSTATS_LEN            CONSTSTRLEN(CMD_JOBSTACKS)
#define CMD_USE_LEN                 CONSTSTRLEN(CMD_USE)
#define CMD_WATCH_LEN               CONSTSTRLEN(CMD_WATCH)
#define CMD_IGNORE_LEN              CONSTSTRLEN(CMD_IGNORE)
#define CMD_LIST_TUBES_LEN          CONSTSTRLEN(CMD_LIST_TUBES)

static int conn_has_reserved_job(conn_t *c) {
    return dlist_length(c->reserverd_jobs) != 0;
}

static int64_t conn_tickat(conn_t *c) {
    int margin = 0, should_timeout = 0;
    int64_t t = INT64_MAX;

    if (conn_waiting(c)) {  /* XXX: for what ? */
        margin = SAFETY_MARGIN;
    }

    if (conn_has_reserved_job(c)) {
        t = conn_sonnest_job(c)->rec.deadline_at - ustime() - margin;
        should_timeout = 1;
    }

    if (c->pending_timeout >= 0) {
        t = min(t, (int64_t)c->pending_timeout * 1000000);
    }

    if (should_timeout) {
        return ustime() + t;
    }
    return 0;
}

job_t *conn_soonest_job(conn_t *c) {
    job_t *j = NULL;
    job_t *soonest = c->soonest_job;

    if (!soonest) {
        dlist_iter iter;
        dlist_node *node;
        dlist_rewind(c->reserved_jobs, &iter);

        while ((node = dlist_next(&iter))) {
            j = (job_t *)node->value;
            if (j->rec.deadline_at <= (soonest ? : j)->rec.deadline_at) {
                soonest = j;
            }
        }
    }
    c->soonest_job = soonest;
    return soonest;
}

int conn_less(void *conn_a, void *conn_b) {
    return (conn_t *)conn_a->tickat < (conn_t *)conn_b->tickat;
}

int conn_record(void *conn, int pos) {
    ((conn_t *)conn)->tickpos = i;
}

conn_t *conn_create(int fd, char start_state, tube_t *use,
        tube_t *watch) {
    job_t *j;
    conn_t *c = (conn_t *)calloc(1, sizeof(*c));
    if (vector_init(&c->watch, INIT_WATCH_NUM, sizeof(tube_t *)) < 0) {
        free(c);
        return NULL;
    }

    if (vector_push(&c->watch, watch) < 0) {
        vector_destroy(&c->watch);
        free(c);
        return NULL;
    }
    tube_iref(watch);
    ++watch->watching_cnt;

    TUBE_ASSIGN(c->use, use);
    ++use->using_cnt;

    c->sock.fd = fd;
    c->state = start_state;
    c->pending_timeout = -1;
    c->tickpos = -1;
    dlist_init(&c->reserved_jobs);

    /* stats */
    ++tasque_srv.cur_conn_cnt;
    ++tasque_srv.tot_conn_cnt;
    return c;
}

void conn_set_producer(conn_t *c) {
    if (c->type & CONN_TYPE_PRODUCER) return;
    c->type |= CONN_TYPE_PRODUCER;
    ++tasque_srv.cur_producer_cnt;  /* stats */
}

void conn_set_worker(conn_t *c) {
    if (c->type & CONN_TYPE_WORKER) return;
    c-type |= CONN_TYPE_WORKER;
    ++tasque_srv.cur_worker_cnt;    /* stats */
}

int conn_deadline_soon(conn_t *c) {
    int64_t t = ustime();
    job_t *j = conn_sonnest_job(c);
    return j && t >= j->rec.deadline_at - SAFETY_MARGIN;
}

int conn_ready(conn_t *c) {
    size_t  i;
    for (i = 0; i < c->watch.slots; ++i) {
        if ((tube_t*)vector_get_at(i)->ready_jobs.len) {
            return 1;
        }
    }
    return 0;
}

void conn_close(conn_t *c) {
    event_regis(&tasque_srv.evt, &c->sock, EVENT_DEL);

    if (tasque_srv.verbose) {
        printf("close %d\n", c->sock.fd);
    }

    if (c->sockfd >= 0) {
        close(c->sock.fd);
        c->sock.fd = -1;
    }

    job_free(c->in_job);

    /* TODO */
}


/* TODO */
void conn_cron(void *tickarg, int ev);

/* Always returns at least 2 if a match is found. Return 0 if no match. */
static int scan_eol(const char *s, int size) {
    char *match;
    match = memchr(s, '\r', size - 1);
    if (!match) return 0;

    /* this is safe because we only scan size - 1 chars above. */
    if (match[1] == '\n') return match - s + 2;
    return 0;
}

static int cmd_len(conn_t *c) {
    return scan_eol(c->cmd, c->cmd_read);
}

static void read_from_client(void *arg, int ev) {
    conn_t *conn = (conn_t *)arg;
    int r, to_read;
    job_t *j;

    switch (c->state) {
    case STATE_WANTCOMMAND:
        r = read(c->sock.fd, c->cmd + c->cmd_read, 
                LINE_BUF_SIZE - c->cmd_read);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno = EINTR) {
                return;
            }
            conn_close(c);
        }

        if (r == 0) return conn_close(c);   /* client close connection */
        c->cmd_read += r;

        c->cmd_len = cmd_len(c); 
        /* when c->cmd_len > 0, we have a complete command */
        if (c->cmd_len) return do_cmd(c);
        if (c->cmd_read == LINE_BUF_SIZE) {
            c->cmd_read = 0;    /* discard the input so far */
            return reply_msg(c, MSG_BAD_FORMAT);
        }

        /* otherwise we have an incomplete line, so just keep waiting */
        break;
    case STATE_BITBUCKET:
        /* XXX */
    }
}

void conn_accept(const int fd, const short which, void *arg) {
    char remote_ip[INET4_IP_LEN] = {};
    int remote_port = 0;
    int cli_fd;
    conn_t *c;
    int ret;
    evtent_t *sock = (evtent_t *)arg;
    if ((cli_fd = tcp_accept(fd, remote_ip, &remote_port)) < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "accept failed:%s\n", strerror(errno));
        }
        return;
    }

    if (tasque_srv.verbose) {
        printf("accept connection %s:%d\n", remote_ip, remote_port);
    }

    if (net_nonblock(cli_fd) < 0) {
        fprintf(stderr, "net_nonblock failed:%s\n", strerror(errno));
        close(cli_fd);
        if (tasque_srv.verbose) {
            printf("close connection %s:%d\n", remote_ip, remote_port);
        }
        return;
    }

    c = conn_create(cli_fd, STATE_WANTCOMMAND, 
            tasque_srv.default_tube, tasque_srv.default_tube);
    /* TODO memory leak check */
    if (!c) {
        fprintf(stderr, "conn_create failed\n");
        close(cli_fd);
        if (tasque_srv.verbose) {
            printf("close connection %s:%d\n", remote_ip, remote_port);
        }
        return;
    }
    c->sock.x = c;
    c->sock.f = (handle_fn)read_from_client;
    c->sock.fd = cli_fd;

    ret = event_regis(&tasque_srv.evt, &c->sock, EVENT_RD);
    if (ret < 0) {
        fprintf(stderr, "event_regis failed\n");
        close(cli_fd);
        if (tasque_srv.verbose) {
            printf("close connection %s:%d\n", remote_ip, remote_port);
        }
        return;
    }
}
