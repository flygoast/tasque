#ifndef __SRV_H_INCLUDED__
#define __SRV_H_INCLUDED__

#include <stdint.h>
#include "tube.h"
#include "conn.h"
#include "heap.h"
#include "event.h"
#include "hash.h"
#include "set.h"

typedef struct server_st {
    int         port;
    char        *host;
    char        *user;
    evtent_t    sock;
    event_t     evt;
    heap_t      conns;
    set_t       tubes;
    tube_t      *default_tube;
    int         verbose;
    int         drain_mode;
    int64_t     started_at;

    int         cur_conn_cnt;
    int         cur_worker_cnt;
    uint32_t    tot_conn_cnt;
    int         cur_producer_cnt;

    uintptr_t   next_job_id;
    int         ready_cnt;

    stats_t     global_stat;
    uint64_t    op_cnt[TOTAL_OPS];
    uint64_t    timeout_cnt;
    hash_t      all_jobs;
} server_t;

extern server_t tasque_srv;

void srv_init();
void srv_serve();
void srv_destroy();

#endif /* __SRV_H_INCLUDED__ */
