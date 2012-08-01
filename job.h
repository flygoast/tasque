#ifndef __JOB_H_INCLUDED__
#define __JOB_H_INCLUDED__

#include <stdint.h>
#include "tube.h"

typedef struct job_st job_t;

typedef struct job_record_st {
    uint64_t    id;
    uint32_t    pri;
    int64_t     delay;
    int64_t     ttr;
    int32_t     body_size;
    int64_t     created_at;
    int64_t     deadline_at;
    uint32_t    reserve_cnt;
    uint32_t    timeout_cnt;
    uint32_t    release_cnt;
    uint32_t    bury_cnt;
    uint32_t    kick_cnt;
    uint8_t     state;
} jobrec_t;

struct job_st {
    jobrec_t   rec;
    /* bookeeping fields; these are in-memory only */
    job_t      *prev;
    job_t      *next;
    job_t      *ht_next;   /* next job in a hash table list */
    size_t      heap_index; /* where is this job in its current heap */
    void        *reserver;
};

void job_set_heap_pos(void *, int);
int job_pri_less(void *, void *);
int job_delay_less(void *, void *);
job_t *job_create(int body_size);
job_t *job_find(uint64_t jobid);
job_t *job_create_with_id(uint32_t pri, int64_t delay, int64_t ttr,
        int body_size, tube_t *tube, uint64_t);

#endif /* __JOB_H_INCLUDED__ */
