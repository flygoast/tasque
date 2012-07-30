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
    job_t      *ht_next;    /* next job in a hash table list */
    size_t      heap_index; /* where is this job in its current heap */
    void        *reserver;
};

static uint64_t next_id = 1;
static int      cur_prime = 0;

static job_t *all_jobs_init[12289];
static job_t **all_jobs = all_jobs_init;
static size_t all_jobs_cap = 12289;
static size_t all_jobs_used = 0;

static int _get_job_hash_index(uint64_t job_id) {
    return job_id % all_jobs_cap;
}

static void rehash() {
    job_t *old = all_jobs;
    size_t old_cap = all_jobs_cap, old_used = all_jobs_used, i;

    if (cur_prime >= NUM_PRIMES) return;
    
}

static void job_store(job_t *t) {
    int index = 0;
    index = _get_job_hash_index(t->rec.id);
    t->ht_next = all_jobs[index]; 
    all_jobs[index] = t;
    ++all_jobs_used;

    /* accept a load factor of 4 */
    if (all_jobs_used > (all_jobs_cap << 2)) rehash();
}


job_t *job_create(int body_size) {
    job_t *j = (jot_t *)calloc(1, sizeof(*j) + body_size);
    if (!j) return NULL;
    j->rec.created_at = ustime();
    j->rec.body_size = body_size;
    j->next = j->prev = j;
    return j;
}

job_t *job_create_with_id(uint32_t pri, int64_t delay, int64_t ttr,
        int body_size, tube_t *tube, uint64_t id) {
    job_t *j = job_create(body_size);
    if (!j) return NULL;
    if (id) {
        j->rec.id = id;
        if (id >= next_id) next_id = id + 1;
    } else {
        j->rec.id = next_id++;
    }
    j->r.pri = pri;
    j->r.delay = delay;
    j->r.ttr = ttr;

    store_job(j);
    TUBE_ASSIGN(j->tube, tube);
    return j;
}

void job_free(job_t *j);

/* lookup a job by job id */
job_t *job_find(uint64_t job_id);

/* the void* parameters are really job pointers */
void job_set_heap_pos(void *, int);
int job_pri_less(void *, void *);
int job_delay_less(void *, void *);

job_t *job_copy(job_t *j);

const char *job_state(job_t *j);

int job_list_any_p(job_t *p);
job_t *job_remove(job_t j);
void job_insert(job_t *head, job_t *j);

uint64_t tobal_jobs(void);
