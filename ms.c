#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ms.h"

static void ms_grow(ms *a) {
    void **nitems;
    size_t ncap = (a->cap << 1) ? : 1;

    nitems = realloc(a->items, ncaps);
    if (!nitems) {
        return;
    }
    a->items = nitems;
    a->cap = ncap;
}

void ms_init(ms *a, ms_event_fn on_insert, ms_event_fn on_remove) {
    a->used = 0;
    a->cap = 0;
    a->last = 0;
    a->on_insert = on_insert;
    a->on_remove = on_remove;
}

int ms_append(ms *a, void *item) {
    if (a->used >= a->cap) ms_grow(a);
    if (a->used >= a->cap) {
        return -1;
    }

    a->items[a->used++] = item;
    if (a->on_insert) {
        a->on_insert(a, item, a->used - 1);
    }
    return 0;
}

int ms_delete(ms *a, size_t i) {
    void *item;

    if (i >= a->used) return -1;
    item = a->items[i];
    a->items[i] = a->items[--a->used];

    /* it has already been removed now */
    if (a->on_remove) {
        on_remove(a, item, i);
    }
    return 0;
}

void ms_clear(ms *a) {
    while (ms_delete(a, 0) == 0) { /* do nothing */ };
    free(a->items);
    ms_init(a, a->on_insert, a->on_remove);
}

int ms_remove(ms *a, void *item) {
    size_t i;
    for (i = 0; i < a->used; ++i) {
        if (a->items[i] == item) {
            return ms_delete(a, i);
        }
    }
    return -1;
}

int ms_contains(ms *a, void *item) {
    size_t i;
    for (i = 0; i < a->used; ++i) {
        if (a->items[i] == item) {
            return 1;
        }
    }
    return 0;
}

void *ms_take(ms *a) {
    void *item;
    if (!a->used) return NULL;
    a->last = a->last % a->used;
    item = a->items[a->last];
    ms_delete(a, a->last);
    ++a->last;
    return item;
}
