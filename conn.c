#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <ctype.h>
#include <inttypes.h>
#include <netinet/in.h>
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
#define URGENT_THRESHOLD        1024
#define JOB_DATA_LIMIT          ((1 << 16) - 1)

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

#define CONSTSTRLEN(m)              (sizeof(m) - 1)

#define CMD_PEEK_READY_LEN          CONSTSTRLEN(CMD_PEEK_READY)
#define CMD_PEEK_DELAYED_LEN        CONSTSTRLEN(CMD_PEEK_DELAYED)
#define CMD_PEEK_BURIED_LEN         CONSTSTRLEN(CMD_PEEK_BURIED)
#define CMD_PEEKJOB_LEN             CONSTSTRLEN(CMD_PEEKJOB)
#define CMD_RESERVE_LEN             CONSTSTRLEN(CMD_RESERVE)
#define CMD_RESERVE_TIMEOUT_LEN     CONSTSTRLEN(CMD_RESERVE_TIMEOUT)
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
#define MSG_BURIED_FMT              "BURIED %ld\r\n"
#define MSG_INSERTED_FMT            "INSERTED %ld\r\n"
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

static unsigned char which_cmd(conn_t *c) {
#define TEST_CMD(s, c, o) \
    if (strncmp((s), (c), CONSTSTRLEN(c)) == 0) return (o)
    TEST_CMD(c->cmd, CMD_PUT, OP_PUT);
    TEST_CMD(c->cmd, CMD_PEEKJOB, OP_PEEKJOB);
    TEST_CMD(c->cmd, CMD_PEEK_READY, OP_PEEK_READY);
    TEST_CMD(c->cmd, CMD_PEEK_DELAYED, OP_PEEK_DELAYED);
    TEST_CMD(c->cmd, CMD_PEEK_BURIED, OP_PEEK_BURIED);
    TEST_CMD(c->cmd, CMD_RESERVE_TIMEOUT, OP_RESERVE_TIMEOUT);
    TEST_CMD(c->cmd, CMD_RESERVE, OP_RESERVE);
    TEST_CMD(c->cmd, CMD_DELETE, OP_DELETE);
    TEST_CMD(c->cmd, CMD_RELEASE, OP_RELEASE);
    TEST_CMD(c->cmd, CMD_BURY, OP_BURY);
    TEST_CMD(c->cmd, CMD_KICK, OP_KICK);
    TEST_CMD(c->cmd, CMD_TOUCH, OP_TOUCH);
    TEST_CMD(c->cmd, CMD_JOBSTATS, OP_JOBSTATS);
    TEST_CMD(c->cmd, CMD_STATS_TUBE, OP_STATS_TUBE);
    TEST_CMD(c->cmd, CMD_STATS, OP_STATS);
    TEST_CMD(c->cmd, CMD_USE, OP_USE);
    TEST_CMD(c->cmd, CMD_WATCH, OP_WATCH);
    TEST_CMD(c->cmd, CMD_IGNORE, OP_IGNORE);
    TEST_CMD(c->cmd, CMD_LIST_TUBES_WATCHED, OP_LIST_TUBES_WATCHED);
    TEST_CMD(c->cmd, CMD_LIST_TUBE_USED, OP_LIST_TUBE_USED);
    TEST_CMD(c->cmd, CMD_LIST_TUBES, OP_LIST_TUBES);
    TEST_CMD(c->cmd, CMD_QUIT, OP_QUIT);
    TEST_CMD(c->cmd, CMD_PAUSE_TUBE, OP_PAUSE_TUBE);
    return OP_UNKNOWN;
}

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

static void conn_reset(conn_t *c) {
    event_regis(&tasque_srv.evt, &c->sock, EVENT_RD);
    c->reply_sent = 0;
    c->state = STATE_WANTCOMMAND;
}



/* Copy up to body_size trailing bytes into the job, then the rest
 * into the cmd buffer. If c->in_job exists, this assume that
 * c->in_job->body is empty.
 * This function is idempotent. */
static void fill_extra_data(conn_t *c) {
    int extra_bytes, job_data_bytes = 0, cmd_bytes;

    if (c->sock.fd < 0) return; /* the connection was closed */
    if (!c->cmd_len) return; /* we don't have a complete command */

    /* how many extra_bytes did we read? */
    extra_bytes = c->cmd_read - c->cmd_len;

    if (c->in_job) { /* we are reading job content */
        job_data_bytes = min(extra_bytes, c->in_job->rec.body_size);
        memcpy(c->in_job->body, c->cmd + c->cmd_len, job_data_bytes);
        c->in_job_read = job_data_bytes;
    } else if (c->in_job_read) {
        /* we are in bit-bucket mode, throwing away data */
        job_data_bytes = min(extra_bytes, c->in_job_read);
        c->in_job_read -= job_data_bytes;
    }

    /* how many bytes are left to go into the future cmd? */
    cmd_bytes = extra_bytes - job_data_bytes;
    memmove(c->cmd, c->cmd + c->cmd_len + job_data_bytes, cmd_bytes);
    c->cmd_read = cmd_bytes;
    c->cmd_len = 0; /* we no longer know the length of the new command */
}


//void conn_rm_dirty(conn_t *c) {
//    dlist_node *dn;
//    dn = dlist_search_key(&tasque_srv.dirty_conns, c);
//    if (!dn) return;
//    dlist_delete_node(&tasque_srv.dirty_conns, dn);
//}

/* -------------- Reply association functions ---------------- */
#define reply_msg(c, m) \
    reply((c), (m), CONSTSTRLEN(m), STATE_SENDWORD)

static void reply(conn_t *c, char *line, int len, int state) {
    if (!c) return;

    if (event_regis(&tasque_srv.evt, &c->sock, EVENT_WR) != 0) {
        fprintf(stderr, "event regis failed\n");
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

static void reply_line(conn_t *c, int state, const char *fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(c->reply_buf, LINE_BUF_SIZE, fmt, ap);
    va_end(ap);

    /* Make sure the buffer was big enough. If not, we have a bug. */
    if (ret >= LINE_BUF_SIZE) {
        return reply_msg(c, MSG_INTERNAL_ERROR);
    }
    return reply(c, c->reply_buf, ret, state);
}

//static void reply_job(conn_t *c, job_t *j, const char *word) {
//    /* tell this connection which job to send */
//    c->out_job = j;
//    c->out_job_sent = 0;
//    return reply_line(c, STATE_SENDJOB, "%s %" PRIu64 " %u\r\n",
//            word, j->rec.id, j->rec.body_size - 2);
//}


#define skip_and_reply_msg(c, n, m) \
    skip_and_reply(c, n, m, CONSTSTRLEN(m))

/* Skip `n' bytes job content received, and reply the content
 * refered in `line' of length of 'len'. */
static void skip_and_reply(conn_t *c, int n, char *line, int len) {
    /* Invert the meaning of 'in_job_read' while throwing away data
     * -- it counts the bytes that remain to be thrown away. */
    c->in_job = NULL;
    c->in_job_read = n;
    fill_extra_data(c);

    if (c->in_job_read == 0) {
        return reply(c, line, len, STATE_SENDWORD);
    }

    /* we enter bit-bucket mode */
    c->reply = line;
    c->reply_len = len;
    c->reply_sent = 0;
    c->state = STATE_BITBUCKET; 
    return;
}

#define conn_is_waiting(c)  ((c)->type & CONN_TYPE_WAITING)

static int conn_has_reserved_job(conn_t *c) {
    return dlist_length(&c->reserved_jobs) != 0;
}

/* remove this connection to associated tubes' waiting set */
static void conn_remove_waiting(conn_t *c) {
    tube_t *t;
    size_t  i;

    if (!conn_is_waiting(c)) return;

    c->type &= ~CONN_TYPE_WAITING;
    --tasque_srv.global_stat.waiting_cnt;

    for (i = 0; i < c->watch.used; ++i) {
        t = (tube_t *)c->watch.items[i];
        --t->stats.waiting_cnt;
        set_remove(&t->waiting_conns, c);
    }
}


/* add this connection to associated tubes' waiting set */
static void conn_enqueue_waiting(conn_t *c) {
    tube_t *t;
    size_t i;

    ++tasque_srv.global_stat.waiting_cnt;
    c->type |= CONN_TYPE_WAITING;

    for (i = 0; i < c->watch.used; ++i) {
        t = c->watch.items[i];
        ++t->stats.waiting_cnt;
        set_append(&t->waiting_conns, c);
    }
}

static void wait_for_job(conn_t *c, int timeout) {
    c->state = STATE_WAIT;
    /* add this connection to associated tubes' waiting set */
    conn_enqueue_waiting(c); 

    /* Set the pending timeout to the requested timeout amount */
    c->pending_timeout = timeout;

    /* Only care if the connection hang up. */
    c->ev = EVENT_HUP;
    c->tickat = conn_tickat(c);

    /* delete old tick event */
    if (c->tickpos > -1) {
        heap_remove(&tasque_srv.conns, c->tickpos);
    }

    if (c->tickat) {
        heap_insert(&tasque_srv.conns, c);
    }
    assert(dlist_add_node_head(&tasque_srv.dirty_conns, c) != NULL);
}

/* the connection should be waked up at some tick */
int64_t conn_tickat(conn_t *c) {
    int margin = 0, should_timeout = 0;
    int64_t t = INT64_MAX;

    if (conn_is_waiting(c)) {
        margin = SAFETY_MARGIN;
    }

    if (conn_has_reserved_job(c)) {
        t = conn_soonest_reserved_job(c)->rec.deadline_at 
            - ustime() - margin;
        should_timeout = 1;
    }

    if (c->pending_timeout >= 0) {
        t = min(t, (int64_t)c->pending_timeout * 1000000);
        should_timeout = 1;
    }

    if (should_timeout) {
        return ustime() + t;
    }
    return 0;
}

/* return the reserved job with the earlist deadline,
 * or NULL if there is no reserved job */
job_t *conn_soonest_reserved_job(conn_t *c) {
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

//int conn_less(void *conn_a, void *conn_b) {
//    return ((conn_t *)conn_a)->tickat < ((conn_t *)conn_b)->tickat;
//}
//
//void conn_record(void *conn, int pos) {
//    ((conn_t *)conn)->tickpos = pos;
//}

conn_t *conn_create(int fd, char start_state, tube_t *use,
        tube_t *watch) {
    conn_t *c = (conn_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;

    set_init(&c->watch, (set_event_fn)on_watch, (set_event_fn)on_ignore);
    if (set_append(&c->watch, watch) != 0) {
        free(c);
        return NULL;
    }

    c->use = use;
    tube_iref(c->use);
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

/* return true if `c' has a reserved job with less than one second
 * until its deadline. */
int conn_deadline_soon(conn_t *c) {
    int64_t t = ustime();
    job_t *j = conn_soonest_reserved_job(c);
    return j && t >= j->rec.deadline_at - SAFETY_MARGIN;
}

int conn_has_ready_job(conn_t *c) {
    size_t  i;

    for (i = 0; i < c->watch.used; ++i) {
        if (((tube_t*)c->watch.items[i])->ready_jobs.len) {
            return 1;
        }
    }
    return 0;
}

void conn_free(conn_t *c) {
    if (tasque_srv.verbose) {
        printf("close connection %s:%d\n", c->remote_ip, c->remote_port);
    }

    if (c->sock.fd >= 0) {
        close(c->sock.fd);
        c->sock.fd = -1;
    }

    if (c->in_job) job_free(c->in_job);
    if (c->out_job && !c->out_job->rec.id) job_free(c->out_job);

    c->in_job = c->out_job = NULL;
    c->in_job_read = 0;

    if (c->type & CONN_TYPE_PRODUCER) {
        --tasque_srv.cur_producer_cnt;
    }
    if (c->type & CONN_TYPE_WORKER) {
        --tasque_srv.cur_worker_cnt;
    }
    --tasque_srv.cur_conn_cnt;
//    conn_remove_waiting(c);
//    if (conn_has_reserved_job(c)) {
//        enqueue_reserved_jobs(c);
//    }

    set_clear(&c->watch);
    --c->use->using_cnt;
    tube_dref(c->use);
    c->use = NULL;

    if (c->tickpos > -1) {
        heap_remove(&tasque_srv.conns, c->tickpos);
    }

    /* DELETE dirty */

    set_destroy(&c->watch);
    dlist_destroy(&c->reserved_jobs);
    --tasque_srv.cur_conn_cnt;
    --tasque_srv.tot_conn_cnt;
    free(c);

}

void conn_close(conn_t *c) {
    event_regis(&tasque_srv.evt, &c->sock, EVENT_DEL);
    conn_free(c);
}


void conn_tick(void *tickarg, int ev) {
    /* do nothing */
}

/* Always returns at least 2 if a match is found. Return 0 if no match. */
static int scan_eol(const char *s, int size) {
    char *match;
    match = memchr(s, '\r', size - 1);
    if (!match) return 0;

    /* this is safe because we only scan size - 1 chars above. */
    if (match[1] == '\n') return match - s + 2;
    return 0;
}

/* Read a priority value from the given buffer and place it in 'pri'.
 * Update `end' to point to the address after the last character
 * consumed. `pri' and `end' can be NULL. If they are both NULL,
 * read_pri() will do the conversion and return the status code 
 * but not update any values. This is an easy way to check for 
 * errors.
 *
 * If `end' is NULL, read_pri() will also check that the entire 
 * input string was consumed and return an error code otherwise.
 *
 * Return 0 on success, or nonzero on failure.
 *
 * If a failure occured, `pri' and `end' are not modified. */
static int read_pri(uint32_t *pri, const char *buf, char **end) {
    char *tend;
    uint32_t tpri;

    while (buf[0] == ' ') ++buf;
    if (!isdigit(buf[0])) return -1;
    tpri = strtoul(buf, &tend, 10);
    if (tend == buf) return -1;
    if (errno && errno != ERANGE) return -1;
    if (!end && tend[0] != '\0') return -1;

    if (pri) *pri = tpri;
    if (end) *end = tend;

    return 0;
}

/* Read a delay value from the given buffer and place it in `delay'.
 * The interface and behavior are analogous to read_pri(). */
static int read_delay(int64_t *delay, const char *buf, char **end) {
    int ret;
    uint32_t delay_sec;

    ret = read_pri(&delay_sec, buf, end);
    if (ret < 0) {
        return ret; /* some error */
    }
    *delay = ((int64_t)delay_sec) * 1000000;
    return 0;
}
    
/* Read a timeout value from the given buffer and place it in `ttr'.
 * The interface and behavior are the same as in read_delay(). */
static int read_ttr(int64_t *ttr, const char *buf, char **end) {
    return read_delay(ttr, buf, end);
}

/* Read a tube name from the given buffer moving the buffer to
 * the name start */
//static int read_tube_name(char **tubename, char *buf, char **end) {
//    size_t len;
//
//    while (buf[0] == ' ') ++buf;
//    len = strspn(buf, NAME_CHARS);
//    if (len == 0) return -1;
//    if (tubename) *tubename = buf;
//    if (end) *end = buf + len;
//    return 0;
//}

/****************************************/
/* TODO */
static void reserve_job(conn_t *c, job_t *j) {
    j->rec.deadline_at = ustime() + j->rec.ttr;
    ++tasque_srv.global_stat.reserved_cnt;
    ++j->tube->stat.reserved_cnt;
    ++j->rec.reserve_cnt;
    j->rec.state = JOB_RESERVED;

    dlist_add_node_head(&c->reserved_jobs, j);
    j->reserver = c;
    if (c->soonest_job && 
            j->rec.deadline_at < c->soonest_job->rec.deadline_at) {
        c->soonest_job = j;
    }

    return reply_job(c, j, MSG_RESERVED);
}

static job_t *next_eligible_job(int64_t now) {
    tube_t *t;
    size_t i;
    job_t *j = NULL, *candidate;

    for (i = 0; i < tasque_srv.tubes.used; ++i) {
        t = tasque_srv.tubes.items[i];
        if (t->pause) {
            if (t->deadline_at > now) continue;
            t->pause = 0;
        }

        if (t->waiting_conns.used && t->ready_jobs.len) {
            candidate = t->ready_jobs.data[0];
            if (!j || job_pri_less(candidate, j)) {
                j = candidate;
            }
        }
    }
    return j;
}

static void process_queue() {
    job_t *j;
    conn_t *c;
    int64_t now = ustime();
    while ((j = next_eligible_job(now))) {
        heap_remove(&j->tube->ready_jobs, j->heap_index);
        --tasque_srv.ready_cnt;
        if (j->rec.pri < URGENT_THRESHOLD) {
            --tasque_srv.global_stat.urgent_cnt;
            --j->tube->stat.urgent_cnt;
        }

        c = set_take(&j->tube->waiting_conns);
        conn_remove_waiting(c);
        if (conn_is_waiting) {
            reserve_job(c, j);
        }
    }
}

//
//static int bury_job(job_t *j) {
//    if (!dlist_add_node_head(&j->tube->buried_jobs, j)) {
//        return -1;
//    }
//
//    ++tasque_srv.global_stat.buried_cnt;
//    ++j->tube->stat.buried_cnt;
//    j->rec.state = JOB_BURIED;
//    j->reserver = NULL;
//    ++j->rec.bury_cnt;
//    return 0;
//}


static int enqueue_job(job_t *j, int64_t delay) {
    int ret;
    j->reserver = NULL;

    if (delay) {
        j->rec.deadline_at = ustime() + delay;
        ret = heap_insert(&j->tube->delay_jobs, j);
        if (ret < 0) return -1;
        j->rec.state = JOB_DELAYED;
    } else {
        ret = heap_insert(&j->tube->ready_jobs, j);
        if (ret < 0) return -1;
        j->rec.state = JOB_READY;
        ++tasque_srv.ready_cnt;
        if (j->rec.pri < URGENT_THRESHOLD) {
            ++tasque_srv.global_stat.urgent_cnt;
            ++j->tube->stats.urgent_cnt;
        }
    }

//    process_queue();
    return 0;
}

static void enqueue_incoming_job(conn_t *c) {
    int ret;
    job_t *j = c->in_job;

    c->in_job = NULL; /* the connection no longer owns this job */
    c->in_job_read = 0;

    /* check if the trailer is present and correct */
    if (memcmp(j->body + j->rec.body_size - 2, "\r\n", 2)) {
        job_free(j);
        return reply_msg(c, MSG_EXPECTED_CRLF);
    }

    if (tasque_srv.verbose >= 2) {
        printf("<%s:%d job %ld\n", 
                c->remote_ip, c->remote_port, (long)j->rec.id);
    }

    if (tasque_srv.drain_mode) {
        job_free(j);
        return reply_msg(c, MSG_DRAINING);
    }

    /* we have a complete job, so let's stick it in the pqueue */
    ret = enqueue_job(j, j->rec.delay);
    if (ret < 0) {
        job_free(j);
        return reply_msg(c, MSG_INTERNAL_ERROR);
    }

    ++tasque_srv.global_stat.total_jobs_cnt;
    ++j->tube->stats.total_jobs_cnt;

    return reply_line(c, STATE_SENDWORD, MSG_INSERTED_FMT,j->rec.id);
}


//static job_t *remove_reserved_job(conn_t *c, job_t *j) {
//    dlist_node *dn;
//    if (!j || j->rec.state != JOB_BURIED) return NULL;
//    dn = dlist_search_key(&tasque_srv.buried_jobs, j);
//    if (!dn) return NULL;
//    dlist_delete_node(&tasque_srv.buried_jobs, dn);
//    if (j) {
//        --tasque_srv.global_stat.reserved_cnt;
//        --j->tube->stat.reserved_cnt;
//        j->reserver = NULL;
//    }
//    c->soonest_job = NULL;
//    return j;
//}
//
//static job_t *remove_buried_job(job_t *j) {
//    dlist_node *dn;
//    if (!j || j->rec.state != JOB_BURIED) return NULL;
//    dn = dlist_search_key(&tasque_srv.buried_jobs, j);
//    if (!dn) return NULL;
//    dlist_delete_node(&tasque_srv.buried_jobs, dn);
//
//    if (j) {
//        --tasque_srv.global_stat.buried_cnt;
//        --j->tube->stat.buried_cnt;
//    }
//    return j;
//}
//
//static job_t *remove_ready_job(job_t *j) {
//    dlist_node *dn;
//    if (!j || j->rec.state != JOB_READY) return NULL;
//    heap_remove(&j->tube->ready_jobs, j->heap_index);
//    --tasque_srv.ready_cnt;
//    if (j->rec.pri < URGENT_THRESHOLD) {
//        --tasque_srv.global_stat.urgent_cnt;
//        --j->tube->stat.urgent_cnt;
//    }
//    return j;
//}
//
//static job_t *remove_delayed_job(job_t *j) {
//    if (!j || j->rec.state != JOB_READY) return NULL;
//    heap_remove(&j->tube->ready_jobs, j->heap_index);
//    --tasque_srv.ready_cnt;
//    if (j->rec.pri < URGENT_THRESHOLD) {
//        --tasque_srv.global_stat.urgent_cnt;
//        --j->tube->stat.urgent_cnt;
//    }
//    return j;
//}
//
//static uint32_t kick_buried_job(tube_t *t) {
//    int ret;
//    job_t *j;
//    int z;
//
//    if (!tube_has_buried_job(t)) return 0;
//    /* TODO */
//    return ret;
//}
//
//static uint32_t kick_buried_jobs(tube_t *t, uint32_t n) {
//    uint32_t i = 0;
//    for (i = 0; (i < n) &&  kick_buried_job(t); ++i) { /* do nothing */ }
//    return i;
//}
//
//static uint32_t kick_delayed_jobs(tube_t *t, uint32_t n) {
//    uint32_t i = 0;
//    for (i = 0; (i < n) &&  kick_delayed_job(t); ++i) { /* do nothing */ }
//    return i;
//}
//
//static uint32_t kick_jobs(tube_t *t, uint32_t n) {
//    if (tube_has_buried_job(t)) return 
//}

static void do_cmd(conn_t *c) {
    unsigned char type;
    int ret, timeout = -1;
    uint32_t pri, body_size;
//    char *size_buf, *delay_buf, *ttr_buf, *pri_buf, *end_buf, *name;
    char *size_buf, *delay_buf, *ttr_buf, *end_buf;
    int64_t delay, ttr;

//    uint64_t id;
//    tube_t *t = NULL;

    /* NUL-terminate this string so we can use strtol and friends */
    c->cmd[c->cmd_len - 2] = '\0';

    /* check for possible maliciousness */
    if (strlen(c->cmd) != c->cmd_len - 2) {
        return reply_msg(c, MSG_BAD_FORMAT);
    }

    type = which_cmd(c);
    if (tasque_srv.verbose >= 2) {
        printf("<%s:%d command %s\n", c->remote_ip, c->remote_port,
                op_names[type]);
    }

    switch (type) {
    case OP_PUT:
        ret = read_pri(&pri, c->cmd + 4, &delay_buf);
        if (ret < 0) return reply_msg(c, MSG_BAD_FORMAT);

        ret = read_delay(&delay, delay_buf, &ttr_buf);
        if (ret < 0) return reply_msg(c, MSG_BAD_FORMAT);
        
        ret = read_ttr(&ttr, ttr_buf, &size_buf);
        if (ret < 0) return reply_msg(c, MSG_BAD_FORMAT);

        body_size = strtoul(size_buf, &end_buf, 10);
        if (errno) return reply_msg(c, MSG_BAD_FORMAT);

        ++tasque_srv.op_cnt[type];

        if (body_size > JOB_DATA_LIMIT) {
            /* throw away the job body and respond with JOB_TOO_BIG */
            skip_and_reply_msg(c, body_size + 2, MSG_JOB_TOO_BIG);
            return;
        }

        /* don't allow trailing garbage */
        if (end_buf[0] != '\0') return reply_msg(c, MSG_BAD_FORMAT);

        conn_set_producer(c);
        if (ttr < 1000000) { /* 1 second */
            ttr = 1000000;
        }

        c->in_job = job_create(pri, delay, ttr, body_size + 2, c->use, 0);
        if (!c->in_job) {
            /* throw away the job body and respond with OUT_OF_MEMORY */
            fprintf(stderr, "server error: " MSG_OUT_OF_MEMORY);
            skip_and_reply_msg(c, body_size + 2, MSG_OUT_OF_MEMORY);
            return;
        }
        fill_extra_data(c);

        /* it's possible we already have a complete job */
        if (c->in_job_read == c->in_job->rec.body_size) {
            return enqueue_incoming_job(c);
        }
        /* otherwise we have incomplete data, so just keep waiting */
        c->state = STATE_WANTDATA;
        break;
//    case OP_PEEK_READY:
//        /* don't allow trailing garbage */
//        if (c->cmd_len != CMD_PEEK_READY_LEN + 2) {
//            return reply_msg(c, MSG_BAD_FORMAT);
//        }
//        ++tasque_srv.op_cnt[type];
//
//        if (c->use->ready_jobs.len) {
//            j = job_copy(c->use->ready_jobs.data[0]);
//        }
//
//        if (!j) {
//            reply(c, MSG_NOTFOUNT, MSG_NOTFOUND_LEN, STATE_SENDWORD);
//            return;
//        }
//
//        reply_job(c, j, MSG_FOUND);
//        break;
//
//    case OP_PEEK_DELAYED:
//        /* don't allow trailing garbage */
//        if (c->cmd_len != CMD_PEEK_DELAYED_LEN + 2) {
//            return reply_msg(c, MSG_BAD_FORMAT);
//        }
//        ++tasque_srv.op_cnt[type];
//        if (c->use->delay.len) {
//            j = job_copy(c->use->delay.data[0]);
//        }
//
//        if (!j) {
//            return reply(c, MSG_NOTFOUND, MSG_NOTFOUND_LEN, STATE_SENDWORD);
//        }
//        reply_job(c, j, MSG_FOUND);
//        break;
//
//    case OP_PEEK_BURIED:
//        /* don't allow trailing garbage */
//        if (c->cmd_len != CMD_PEEK_BURIED_LEN + 2) {
//            return reply_msg(c, MSG_BAD_FORMAT);
//        }
//        ++tasque_srv.op_cnt[type];
//
//        if (!tube_has_buried_job(c->use)) {
//            return reply(c, MSG_NOTFOUND, MSG_NOTFOUND_LEN, STATE_SENDWORD);
//        } else {
//            /* XXX: it's a stack implementation */
//            j = job_copy();
//            reply_job(c, j, MSG_FOUND);
//        }
//        break;
//    case OP_PEEKJOB:
//        id = strtoull(c->cmd + CMD_PEEK_JOB_LEN, &end_buf, 0);
//        if (errno) return reply_msg(c, MSG_BAD_FORMAT);
//        ++tasque_srv.op_cnt[type];
//
//        /* So, peek is annoying, because some other connection might
//         * free the job while we are still trying to write it out.
//         * So we copy it and then free the copy when it's done sending. */
//        j = job_copy(job_find(id));
//        if (!j) {
//            return reply(c, MSG_NOTFOUND, MSG_NOTFOUND_LEN, STATE_SENDWORD);
//        }
//        reply_job(c, j, MSG_FOUND);
//        break;
    case OP_RESERVE_TIMEOUT:
        timeout = strtol(c->cmd + CMD_RESERVE_TIMEOUT_LEN, &end_buf, 10);
        if (errno) {
            return reply_msg(c, MSG_BAD_FORMAT);
        }
        /* fall through */
    case OP_RESERVE:
        /* don't allow trailing garbage */
        if (type == OP_RESERVE && c->cmd_len != CMD_RESERVE_LEN + 2) {
            return reply_msg(c, MSG_BAD_FORMAT);
        }

        ++tasque_srv.op_cnt[type];
        conn_set_worker(c);

        if (conn_deadline_soon(c) && !conn_has_ready_job(c)) {
            return reply_msg(c, MSG_DEADLINE_SOON);
        }

        /* try to get a new job for this guy */
        wait_for_job(c, timeout);
        process_queue();
        break;
//    case OP_DELETE:
//        id = strtoull(c->cmd + CMD_DELETE_LEN, &end_buf, 10);
//        if (errno) return reply_msg(c, MSG_BAD_FORMAT);
//        ++tasque_srv.opt_cnt[type];
//
//        j = job_find(id);
//        j = remove_reserved_job(c, j) ? :
//            remove_ready_job(j) ? :
//            remove_buried_job(j) ? :
//            remove_delayed_job(j);
//
//        if (!j) {
//            reply(c, MSG_NOTFOUND, MSG_NOTFOUND_LEN, STATE_SENDWORD);
//            return;
//        }
//        ++j->tube->stat.total_delete_cnt;
//        j->rec.state = JOB_INVALID;
//        job_free(j);
//
//        break;
//    case OP_RELEASE:
//        id = strtoull(c->cmd + CMD_RELEASE_LEN, &pri_buf, 10);
//        if (errno) return reply_msg(c, MSG_BAD_FORMAT);
//
//        ret = read_pri(&pri, pri_buf, &delay_buf);
//        if (ret) return reply_msg(c, MSG_BAD_FORMAT);
//
//        ret = read_delay(&delay, delay_buf, NULL);
//        if (ret) return reply_msg(c, MSG_BAD_FORMAT);
//        ++tasque.op_cnt[type];
//
//        j = remove_reserved_job(c, job_find(id));
//
//        if (!j) {
//            return reply(c, MSG_NOTFOUND, MSG_NOTFOUND_LEN,
//                    STATE_SENDWORD);
//        }
//
//        j->rec.pri = pri;
//        j->rec.delay = delay;
//        ++j->rec.release_cnt;
//
//        ret = enqueue_job(j, delay);
//        if (ret < 0) {
//            return reply_serr(c, MSG_INTERNAL_ERROR);
//        }
//        if (ret == 1) {
//            return reply(c, MSG_RELEASED, MSG_RELEASED_LEN,
//                    STATE_SENDWORD);
//        }
//        bury_job(j);
//        reply(c, MSG_BURIED, MSG_BURIED_LEN, STATE_SENDWORD);
//        break;
//
//    case OP_BURY:
//        id = strtoull(c->cmd + CMD_BURY_LEN, &pri_buf, 10);
//        if (errno) return reply_msg(c, MSG_BAD_FORMAT);
//
//        ret = read_pri(&pri, pri_buf, NULL);
//        if (ret) return reply_msg(c, MSG_BAD_FORMAT);
//        ++tasque_srv.op_cnt[type];
//        j = remove_reserved_job(c, job_find(id));
//
//        if (!j) {
//            reply(c, MSG_NOTFOUND, MSG_NOTFOUND_LEN, STATE_SENDWORD);
//            return;
//        }
//        j->rec.pri = pri;
//        ret = bury_job(j, 1);
//        if (ret) return reply_serr(c, MSG_INTERNAL_ERROR);
//        reply(c, MSG_BURIED, MSG_BURIED_LEN, STATE_SENDWORD);
//        break;
//    case OP_KICK:
//        count = strtoull(c->cmd + CMD_KICK_LEN, &end_buf, 10);
//        if (end_buf == c->cmd + CMD_KICK_LEN) {
//            return reply_msg(c, MSG_BAD_FORMAT);
//        }
//        if (errno) return reply_msg(c, MSG_BAD_FORMAT);
//        ++tasque_srv.op_cnt[type];
//
//        i = kick_jobs(c->use, count);
//        return reply_line(c, STATE_SENDWORD, "KICKED %u\r\n", i);
    default:
        return reply_msg(c, MSG_UNKNOWN_COMMAND);
    }
}

static void handle_client(void *arg, int ev) {
    int r, to_read;
    job_t *j;
    conn_t *c = (conn_t *)arg;

    if (ev == EVENT_HUP) {
        conn_close(c);
        return;
    }

    /* Hehe..., everyone love status machine. */
    switch (c->state) {
    case STATE_WANTCOMMAND:
        r = read(c->sock.fd, c->cmd + c->cmd_read, 
                LINE_BUF_SIZE - c->cmd_read);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                /* just continue */
                return;
            }
            conn_close(c);
            return;
        } else if (r == 0) {
            return conn_close(c);   /* client close connection */
        }

        c->cmd_read += r; /* we got some bytes */
        c->cmd_len = scan_eol(c->cmd, c->cmd_read);

        /* when c->cmd_len > 0, we have a complete command */
        if (c->cmd_len) {
            do_cmd(c);
            fill_extra_data(c);
            return;
        }
        /* command line too long */
        if (c->cmd_read == LINE_BUF_SIZE) {
            c->cmd_read = 0;    /* discard the input so far */
            return reply_msg(c, MSG_BAD_FORMAT);
        }

        /* otherwise we have an incomplete line, so just keep waiting */
        break;
    case STATE_WANTDATA:
        j = c->in_job;
        r = read(c->sock.fd, j->body + c->in_job_read, 
                j->rec.body_size - c->in_job_read);
        if (r == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                /* just continue */
                return;
            }
            return conn_close(c);
        } else if (r == 0) { /* client hang up the connection */
            return conn_close(c);
        }
        c->in_job_read += r; /* we got some bytes */

        if (c->in_job_read == j->rec.body_size) {
            /* we've got a complete job content */
            return enqueue_incoming_job(c);
        }
        
        /* continue waiting for imcomplete job data */
        break;
    case STATE_BITBUCKET:
        /* Invert the meaning of 'in_job_read' while throwing
         * away data -- it counts the bytes that remain to 
         * be thrown away. */
        to_read = min(c->in_job_read, BUCKET_BUF_SIZE);
        r = read(c->sock.fd, bucket, to_read);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                /* just continue */
                return;
            }
        } else if (r == 0) {
            return conn_close(c); /* client hung up the connection */
        }
        c->in_job_read -= r; /* we got some bytes */

        if (c->in_job_read == 0) {
            return reply(c, c->reply, c->reply_len, STATE_SENDWORD);
        }
        break;
    case STATE_SENDWORD:
        r = write(c->sock.fd, c->reply + c->reply_sent, 
                c->reply_len - c->reply_sent);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                /* just continue */
                return;
            }
        } else if (r == 0) { /* the client hang up the connection */
            conn_close(c);
            return;
        }
        c->reply_sent += r; /* we sent some bytes */

        if (c->reply_sent == c->reply_len) {
            return conn_reset(c);
        }
        /* otherwise we sent an incomplete reply, so just continue */
        break;
    }
}

void conn_accept(void *arg, int ev) {
    char remote_ip[INET_ADDRSTRLEN] = {};
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
    if (!c) {
        fprintf(stderr, "conn_create failed\n");
        close(cli_fd);
        if (tasque_srv.verbose) {
            printf("close connection %s:%d\n", remote_ip, remote_port);
        }
        return;
    }

    memcpy(c->remote_ip, remote_ip, INET_ADDRSTRLEN);
    c->remote_port = remote_port;

    c->sock.x = c;
    c->sock.f = (handle_fn)handle_client;
    c->sock.fd = cli_fd;
    c->sock.added = 0;

    ret = event_regis(&tasque_srv.evt, &c->sock, EVENT_RD);
    if (ret < 0) {
        fprintf(stderr, "event_regis failed\n");
        conn_free(c);
        if (tasque_srv.verbose) {
            printf("close connection %s:%d\n", remote_ip, remote_port);
        }
        return;
    }
}
