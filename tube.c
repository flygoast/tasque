#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "job.h"
#include "srv.h"
#include "dlist.h"
#include "heap.h"
#include "tube.h"

tube_t *tube_create(const char *name) {
    tube_t *t = (tube_t *)calloc(sizeof(*t), 1);
    if (!t) return NULL;
    strncpy(t->name, name, MAX_TUBE_NAME_LEN - 1);
    if (t->name[MAX_TUBE_NAME_LEN - 1] != '\0') {
        fprintf(stderr, "truncating tube name\n");
    };

    if (heap_init(&t->ready_jobs) != 0) {
        free(t);
        return NULL;
    }
    t->ready_jobs.less = job_pri_less;
    t->ready_jobs.record = job_set_heap_pos;

    if (heap_init(&t->delay_jobs) != 0) {
        free(t);
        return NULL;
    }
    t->delay_jobs.less = job_delay_less;
    t->delay_jobs.record = job_set_heap_pos;
    dlist_init(&t->buried_jobs);
    set_init(&t->waiting_conns, NULL, NULL);
    return t;
}

void tube_free(tube_t *t) {
    heap_destroy(&t->ready_jobs);    
    heap_destroy(&t->delay_jobs);
    dlist_destroy(&t->buried_jobs);
    set_destroy(&t->waiting_conns);
    free(t);
}

void tube_dref(tube_t *t) {
    assert(t);
    if (t->refs < 1) {
        fprintf(stderr, "refs is zero for tube: %s\n", t->name);
        return;
    }

    --t->refs;
    if (t->refs < 1) {
        tube_free(t);
    }
}

void tube_iref(tube_t *t) {
    assert(t);
    ++t->refs;
}

tube_t *tube_find(const char *name) {
    tube_t *t;
    size_t i;

    for (i = 0; i < tasque_srv.tubes.used; ++i) {
        t = tasque_srv.tubes.items[i];
        if (strncmp(t->name, name, MAX_TUBE_NAME_LEN) == 0) {
            return t;
        }
    }
    return NULL;
}

int tube_has_buried_job(tube_t *t) {
    return dlist_length(&t->buried_jobs) != 0;
}
