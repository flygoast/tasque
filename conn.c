#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <inttypes.h>
#include "srv.h"
#include "conn.h"
#include "times.h"
#include "dlist.h"
#include "event.h"
#include "tube.h"
#include "net.h"
#include "job.h"

#define INIT_WATCH_NUM  8
#define min(a, b)       ((a) < (b) ? (a) : (b))

#define SAFETY_MARGIN           1000000         /* 1 second */

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
#define CMD_LIST_TUBE_USED_LEN      CONSTSTRLEN(CMD_LIST_TUBE_USED)
#define CMD_LIST_TUBES_WATCHED_LEN  CONSTSTRLEN(CMD_LIST_TUBES_WATCHED)
#define CMD_STATS_TUBE_LEN          CONSTSTRLEN(CMD_STATS_TUBE)
#define CMD_PAUSE_TUBE_LEN          CONSTSTRLEN(CMD_PAUSE_TUBE)

#define MSG_FOUND                   "FOUND"
#define MSG_NOTFOUND                "NOT_FOUND\r\n"
#define MSG_RESERVED                "RESERVED"
#define MSG_DEADLINE_SOON           "DEADLINE_SOON\r\n"
#define MSG_TIMED_OUT               "TIMED_OUT\r\n"
#define MSG_DELETED                 "DELETED\r\n"
#define MSG_RELEASED                "RELEASED\r\n"
#define MSG_BURIED                  "BURIED\r\n"
#define MSG_TOUCHED                 "TOUCHED\r\n"
#define MSG_BURIED_FMT              "BURIED %"PRIu64"\r\n"
#define MSG_INSERTED_FMT            "INSERTED %"PRIu64"\r\n"
#define MSG_NOT_IGNORED             "NOT_IGNORED\r\n"

#define MSG_NOTFOUND_LEN            CONSTSTRLEN(MSG_NOTFOUND)
#define MSG_DELETED_LEN             CONSTSTRLEN(MSG_DELETED)
#define MSG_TOUCHED_LEN             CONSTSTRLEN(MSG_TOUCHED)
#define MSG_RELEASED_LEN            CONSTSTRLEN(MSG_RELEASED)
#define MSG_BURIED_LEN              CONSTSTRLEN(MSG_BURIED)
#define MSG_NOT_IGNORED_LEN         CONSTSTRLEN(MSG_NOT_IGNORED)

#define MSG_OUT_OF_MEMORY           "OUT_OF_MEMORY\r\n"
#define MSG_INTERNAL_ERROR          "INTERNAL_ERROR\r\n"
#define MSG_DRAINING                "DRAINING\r\n"

#define MSG_BAD_FORMAT              "BAD_FORMAT\r\n"
#define MSG_UNKNOWN_COMMAND         "UNKNOWN_COMMAND\r\n"
#define MSG_EXPECTED_CRLF           "EXPECTED_CRLF\r\n"
#define MSG_JOB_TOO_BIG             "JOB_TOO_BIG\r\n"

#define STATE_WANTCOMMAND       0
#define STATE_WANTDATA          1
#define STATE_SENDJOB           2
#define STATE_SENDWORD          3
#define STATE_WAIT              4
#define STATE_BITBUCKET         5

#define OP_UNKNOWN              0
#define OP_PUT                  1
#define OP_PEEKJOB              2
#define OP_RESERVE              3
#define OP_DELETE               4
#define OP_RELEASE              5
#define OP_BURY                 6
#define OP_KICK                 7
#define OP_STATS                8
#define OP_JOBSTATS             9
#define OP_PEEK_BURIED          10
#define OP_USE                  11
#define OP_WATCH                12
#define OP_IGNORE               13
#define OP_LIST_TUBES           14
#define OP_LIST_TUBE_USED       15
#define OP_LIST_TUBES_WATCHED   16
#define OP_STATS_TUBE           17
#define OP_PEEK_READY           18
#define OP_PEEK_DELAYED         19
#define OP_RESERVE_TIMEOUT      20
#define OP_TOUCH                21
#define OP_QUIT                 22
#define OP_PAUSE_TUBE           23
#define TOTAL_OPS               24

#define STATS_FMT "---\n"                       \
    "current-jobs-urgent: %u\n"                 \
    "current-jobs-ready: %u\n"                  \
    "current-jobs-reserved: %u\n"               \
    "current-jobs-delayed: %u\n"                \
    "current-jobs-buried: %u\n"                 \
    "cmd-put: %" PRIu64 "\n"                    \
    "cmd-peek: %" PRIu64 "\n"                   \
    "cmd-peek-ready: %" PRIu64 "\n"             \
    "cmd-peek-delayed: %" PRIu64 "\n"           \
    "cmd-peek-buried: %" PRIu64 "\n"            \
    "cmd-reserve: %" PRIu64 "\n"                \
    "cmd-reserve-with-timeout: %" PRIu64 "\n"   \
    "cmd-delete: %" PRIu64 "\n"                 \
    "cmd-release: %" PRIu64 "\n"                \
    "cmd-use: %" PRIu64 "\n"                    \
    "cmd-watch: %" PRIu64 "\n"                  \
    "cmd-ignore: %" PRIu64 "\n"                 \
    "cmd-bury: %" PRIu64 "\n"                   \
    "cmd-kick: %" PRIu64 "\n"                   \
    "cmd-touch: %" PRIu64 "\n"                  \
    "cmd-stats: %" PRIu64 "\n"                  \
    "cmd-stats-job: %" PRIu64 "\n"              \
    "cmd-stats-tube: %" PRIu64 "\n"             \
    "cmd-list-tubes: %" PRIu64 "\n"             \
    "cmd-list-tube-used: %" PRIu64 "\n"         \
    "cmd-list-tubes-watched: %" PRIu64 "\n"     \
    "cmd-pause-tube: %" PRIu64 "\n"             \
    "job-timeouts: %" PRIu64 "\n"               \
    "total-jobs: %" PRIu64 "\n"                 \
    "max-job-size: %zu\n"                       \
    "current-tubes: %zu\n"                      \
    "current-connections: %u\n"                 \
    "current-producers: %u\n"                   \
    "current-workers: %u\n"                     \
    "current-waiting: %u\n"                     \
    "total-connections: %u\n"                   \
    "pid: %ld\n"                                \
    "version: %s\n"                             \
    "rusage-utime: %d.%06d\n"                   \
    "rusage-stime: %d.%06d\n"                   \
    "uptime: %u\n"                              \
    "binlog-oldest-index: %d\n"                 \
    "binlog-current-index: %d\n"                \
    "binlog-records-migrated: %" PRId64 "\n"    \
    "binlog-records-written: %" PRId64 "\n"     \
    "binlog-max-size: %d\n"                     \
    "\r\n"

#define STATS_TUBE_FMT "---\n"                  \
        "name: %s\n"                            \
    "current-jobs-urgent: %u\n"                 \
    "current-jobs-ready: %u\n"                  \
    "current-jobs-reserved: %u\n"               \
    "current-jobs-delayed: %u\n"                \
    "current-jobs-buried: %u\n"                 \
    "total-jobs: %" PRIu64 "\n"                 \
    "current-using: %u\n"                       \
    "current-watching: %u\n"                    \
    "current-waiting: %u\n"                     \
    "cmd-delete: %" PRIu64 "\n"                 \
    "cmd-pause-tube: %u\n"                      \
    "pause: %" PRIu64 "\n"                      \
    "pause-time-left: %" PRId64 "\n"            \
    "\r\n"

#define STATS_JOB_FMT "---\n"                   \
        "id: %" PRIu64 "\n"                     \
    "tube: %s\n"                                \
    "state: %s\n"                               \
    "pri: %u\n"                                 \
    "age: %" PRId64 "\n"                        \
    "delay: %" PRId64 "\n"                      \
    "ttr: %" PRId64 "\n"                        \
    "time-left: %" PRId64 "\n"                  \
    "file: %d\n"                                \
    "reserves: %u\n"                            \
    "timeouts: %u\n"                            \
    "releases: %u\n"                            \
    "buries: %u\n"                              \
    "kicks: %u\n"                               \
    "\r\n"

/* this number is pretty arbitrary */
#define BUCKET_BUF_SIZE     1024

static char bucket[BUCKET_BUF_SIZE];

static const char *op_names[] = {
    "<unknown>",
    CMD_PUT,
    CMD_PEEKJOB,
    CMD_RESERVE,
    CMD_DELETE,
    CMD_RELEASE,
    CMD_BURY,
    CMD_KICK,
    CMD_STATS,
    CMD_JOBSTATS,
    CMD_PEEK_BURIED,
    CMD_USE,
    CMD_WATCH,
    CMD_IGNORE,
    CMD_LIST_TUBES,
    CMD_LIST_TUBE_USED,
    CMD_LIST_TUBES_WATCHED,
    CMD_STATS_TUBE,
    CMD_PEEK_READY,
    CMD_PEEK_DELAYED,
    CMD_RESERVE_TIMEOUT,
    CMD_TOUCH,
    CMD_QUIT,
    CMD_PAUSE_TUBE,
};

static void on_watch(set_t *s, void *arg, size_t pos) {
    tube_t *t = (tube_t *)arg;
    tube_iref(t);
    ++t->watching_cnt;
}

static void on_ignore(set_t *s, void *arg, size_t pos) {
    tube_t *t = (tube_t *)arg;
    --t->watching_cnt;
    tube_dref(t);
}

static int conn_has_reserved_job(conn_t *c) {
    return dlist_length(&c->reserved_jobs) != 0;
}

static void conn_reset(conn_t *c) {
    event_regis(&tasque_srv.evt, &c->sock, EVENT_RD);
    if (!dlist_add_node_head(&tasque_srv.dirty_conns, c)) {
        conn_close(c);
        return;
    }

    c->reply_sent = 0;
    c->state = STATE_WANTCOMMAND;
}

static void reply(conn_t *c, char *line, int len, int state) {
    if (!c) return;

    event_regis(&tasque_srv.evt, &c->sock, EVENT_WR);
    if (!dlist_add_node_head(&tasque_srv.dirty_conns, c)) {
        conn_close(c);
        return;
    }

    c->reply = line;
    c->reply_len = len;
    c->reply_sent = 0;
    c->state = state;

    if (tasque_srv.verbose >= 2) {
        printf(">%s:%d reply %.*s\n", c->remote_ip, c->remote_port,
            len - 2, line);
    }
}

void conn_rm_dirty(conn_t *c) {
    dlist_node *dn;
    dn = dlist_search_key(&tasque_srv.dirty_conns, c);
    if (!dn) return;
    dlist_delete_node(&tasque_srv.dirty_conns, dn);
}

#define reply_msg(c, m) reply((c), (m), CONSTSTRLEN(m),STATE_SENDWORD)

static void reply_line(conn_t *c, int state, const char *fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(c->reply_buf, LINE_BUF_SIZE, fmt, ap);
    va_end(ap);

    /* Make sure the buffer was big enough. If not, we have a bug. */
    if (ret >= LINE_BUF_SIZE) return reply_msg(c, MSG_INTERNAL_ERROR);

    return reply(c, c->reply_buf, ret, state);
}

static void reply_job(conn_t *c, job_t *j, const char *word) {
    /* tell this connection which job to send */
    c->out_job = j;
    c->out_job_sent = 0;
    return reply_line(c, STATE_SENDJOB, "%s %" PRIu64 " %u\r\n",
            word, j->rec.id, j->rec.body_size - 2);
}

conn_t *conn_remove_waiting(conn_t *c) {
    tube_t *t;
    size_t  i;

    if (!conn_waiting(c)) return NULL;

    c->type &= ~CONN_TYPE_WAITING;
    --tasque_srv.global_stat.waiting_cnt;

    for (i = 0; i < c->watch.used; ++i) {
        t = (tube_t *)c->watch.items[i];
        --t->stats.waiting_cnt;
        set_remove(&t->waiting_conns, c);
    }
    return c;
}

static int64_t conn_tickat(conn_t *c) {
    int margin = 0, should_timeout = 0;
    int64_t t = INT64_MAX;

    if (conn_waiting(c)) {  /* XXX: for what ? */
        margin = SAFETY_MARGIN;
    }

    if (conn_has_reserved_job(c)) {
        t = conn_soonest_job(c)->rec.deadline_at - ustime() - margin;
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
        dlist_rewind(&c->reserved_jobs, &iter);

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
    return ((conn_t *)conn_a)->tickat < ((conn_t *)conn_b)->tickat;
}

void conn_record(void *conn, int pos) {
    ((conn_t *)conn)->tickpos = pos;
}

conn_t *conn_create(int fd, char start_state, tube_t *use,
        tube_t *watch) {
    //job_t *j;
    conn_t *c = (conn_t *)calloc(1, sizeof(*c));

    set_init(&c->watch, (set_event_fn)on_watch, (set_event_fn)on_ignore);
    if (!set_append(&c->watch, watch)) {
        free(c);
        return NULL;
    }

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
    c->type |= CONN_TYPE_WORKER;
    ++tasque_srv.cur_worker_cnt;    /* stats */
}

int conn_deadline_soon(conn_t *c) {
    int64_t t = ustime();
    job_t *j = conn_soonest_job(c);
    return j && t >= j->rec.deadline_at - SAFETY_MARGIN;
}

int conn_ready(conn_t *c) {
    //size_t  i;
    /*
    for (i = 0; i < c->watch.slots; ++i) {
        if ((tube_t*)vector_get_at(i)->ready_jobs.len) {
            return 1;
        }
    }
    */
    return 0;
}

void conn_close(conn_t *c) {
    event_regis(&tasque_srv.evt, &c->sock, EVENT_DEL);

    if (tasque_srv.verbose) {
        printf("close %d\n", c->sock.fd);
    }

    if (c->sock.fd >= 0) {
        close(c->sock.fd);
        c->sock.fd = -1;
    }

    //job_free(c->in_job);

    /* TODO */
}


/* TODO */
void conn_tick(void *tickarg, int ev);

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

static void handle_client(void *arg, int ev) {
    conn_t *c = (conn_t *)arg;
    int r;
    //int to_read;
    //job_t *j;
    //

    switch (c->state) {
    case STATE_WANTCOMMAND:
        r = read(c->sock.fd, c->cmd + c->cmd_read, 
            LINE_BUF_SIZE - c->cmd_read);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                return;
            }
            conn_close(c);
        }
    
    
        if (r == 0) return conn_close(c);
        return reply_msg(c, MSG_BAD_FORMAT);
    case STATE_SENDWORD:
        r = write(c->sock.fd, c->reply + c->reply_sent, 
                c->reply_len - c->reply_sent);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                return;
            }
            fprintf(stderr, "write failed\n");
            conn_close(c);
            return;
        }
        if (r == 0) return conn_close(c);
        c->reply_sent += r;

        if (c->reply_sent == c->reply_len) {
            return conn_reset(c);
        }
        return;
    }

//    switch (c->state) {
//    case STATE_WANTCOMMAND:
//        r = read(c->sock.fd, c->cmd + c->cmd_read, 
//                LINE_BUF_SIZE - c->cmd_read);
//        if (r < 0) {
//            if (errno == EAGAIN || errno == EWOULDBLOCK ||
//                    errno = EINTR) {
//                return;
//            }
//            conn_close(c);
//        }
//
//        if (r == 0) return conn_close(c);   /* client close connection */
//        c->cmd_read += r;
//
//        c->cmd_len = cmd_len(c); 
//        /* when c->cmd_len > 0, we have a complete command */
//        if (c->cmd_len) return do_cmd(c);
//        if (c->cmd_read == LINE_BUF_SIZE) {
//            c->cmd_read = 0;    /* discard the input so far */
//            return reply_msg(c, MSG_BAD_FORMAT);
//        }
//
//        /* otherwise we have an incomplete line, so just keep waiting */
//        break;
//    case STATE_BITBUCKET:
//        /* XXX */
//    }
}

void conn_accept(void *arg, int ev) {
    char remote_ip[INET4_IP_LEN] = {};
    int remote_port = 0;
    int cli_fd;
    conn_t *c;
    int ret;

    evtent_t *sock = (evtent_t *)arg;
    if ((cli_fd = tcp_accept(sock->fd, remote_ip, &remote_port)) < 0) {
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
    c->sock.f = (handle_fn)handle_client;
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
