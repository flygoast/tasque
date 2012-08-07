#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "srv.h"
#include "job.h"
#include "tube.h"
#include "times.h"

job_t *job_create(int pri, int64_t delay, int64_t ttr,
        int body_size, tube_t *tube, uintptr_t job_id) {
    job_t *j = (job_t *)calloc(1, sizeof(*j) + body_size);
    if (!j) return NULL;
    j->rec.created_at = ustime();
    j->rec.body_size = body_size;
    if (job_id) {
        j->rec.id = job_id;
        if (job_id >= tasque_srv.next_job_id) {
            tasque_srv.next_job_id = job_id + 1;
        }
    } else {
        j->rec.id = tasque_srv.next_job_id++;
    }
    j->rec.pri = pri;
    j->rec.delay = delay;
    j->rec.ttr = ttr;

    if (hash_insert(&tasque_srv.all_jobs, (void *)j->rec.id, j) != 0) {
        free(j);
        return NULL;
    }
    j->tube = tube;
    tube_iref(j->tube);
    return j;
}

void job_free(job_t *j) {
    hash_delete(&tasque_srv.all_jobs, (void *)(j->rec.id));
    free(j);
}

/* lookup a job by job id */
job_t *job_find(uintptr_t job_id) {
    return (job_t *)hash_get_val(&tasque_srv.all_jobs, (void *)job_id);
}

/* the void* parameters are really job pointers */
void job_set_heap_pos(void *arg, int pos) {
    ((job_t *)arg)->heap_index = pos;
}

int job_pri_less(void *ax, void *bx) {
    job_t *a = (job_t *)ax; 
    job_t *b = (job_t *)bx;
    if (a->rec.pri < b->rec.pri) return 1;
    if (a->rec.pri > b->rec.pri) return 0;
    return a->rec.id < b->rec.id;
}

int job_delay_less(void *ax, void *bx) {
    job_t *a = (job_t *)ax; 
    job_t *b = (job_t *)bx;
    if (a->rec.deadline_at < b->rec.deadline_at) return 1;
    if (a->rec.deadline_at > b->rec.deadline_at) return 0;
    return a->rec.id < b->rec.id;
}

job_t *job_copy(job_t *j) {
    job_t *aj = (job_t *)malloc(sizeof(aj));
    if (!aj) {
        return NULL;
    }
    memcpy(aj, j, sizeof(job_t) + j->rec.body_size);
    aj->tube = NULL;
    aj->tube = j->tube;
    tube_iref(aj->tube);
    aj->rec.state = JOB_COPY;
    return aj;
}

const char *job_state(job_t *j) {
    if (j->rec.state == JOB_READY) {
        return "ready";
    }

    if (j->rec.state == JOB_RESERVED) {
        return "reserved";
    }

    if (j->rec.state == JOB_BURIED) {
        return "buried";
    }

    if (j->rec.state == JOB_DELAYED) {
        return "delayed";
    }
    return "invalid";
}
