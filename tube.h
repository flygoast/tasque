#ifndef __TUBE_H_INCLUDED__
#define __TUBE_H_INCLUDED__

#include <stdint.h>
#include "dlist.h"
#include "set.h"
#include "heap.h"
#include "tube.h"

#define MAX_TUBE_NAME_LEN       201

#define TUBE_ASSIGN(a, b)   (tube_dref(a), (a) = (b), tube_iref(a))

typedef struct stats_st {
    uint32_t        urgent_cnt;
    uint32_t        waiting_cnt;
    uint32_t        buried_cnt;
    uint32_t        reserved_cnt;
    uint32_t        pause_cnt;
    uint64_t        total_delete_cnt;
    uint64_t        total_jobs_cnt;
} stats_t;

typedef struct tube_st {
    uint32_t        refs;
    char            name[MAX_TUBE_NAME_LEN];
    heap_t          ready_jobs;
    heap_t          delay_jobs;
    dlist           buried_jobs;
    set_t           waiting_conns;    /* set of conns */
    uint32_t        using_cnt;
    uint32_t        watching_cnt;
    int64_t         pause;
    int64_t         deadline_at;
    stats_t         stats;
} tube_t;


tube_t *tube_create(const char *name);
void tube_free(tube_t *t);
void tube_dref(tube_t *t);
void tube_iref(tube_t *t);
tube_t *tube_find(const char *name);
int tube_has_buried_job(tube_t *t);
tube_t *tube_make_and_insert(const char *name);
void tube_free_and_remove(tube_t *t);
tube_t *tube_find_or_create(const char *name);

#endif /* __TUBE_H_INCLUDED__ */
