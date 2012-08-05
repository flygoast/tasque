#ifndef __JOB_H_INCLUDED__
#define __JOB_H_INCLUDED__

#include <stdint.h>
#include "tube.h"

#define JOB_INVALID         0
#define JOB_READY           1
#define JOB_RESERVED        2
#define JOB_BURIED          3
#define JOB_DELAYED         4
#define JOB_COPY            5

typedef struct job_st job_t;
typedef struct job_record_st {
    uintptr_t   id;
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
    jobrec_t    rec;
    tube_t      *tube;
    size_t      heap_index; /* where is this job in its current heap */
    void        *reserver;
    char        body[];
};

job_t *job_create(int pir, int64_t delay, int64_t ttr,
        int body_size, tube_t *tube, uintptr_t job_id);
void job_free(job_t *j);
job_t *job_find(uintptr_t job_id);
void job_set_heap_pos(void *arg, int pos);
int job_pri_less(void *ax, void *bx);
int job_delay_less(void *ax, void *bx);
job_t *job_copy(job_t *j);
const char *job_state(job_t *j);

#endif /* __JOB_H_INCLUDED__ */
