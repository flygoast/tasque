#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "set.h"

static void set_grow(set_t *a) {
    void **nitems;
    size_t ncap = (a->cap << 1) ? : 1;

    nitems = (void **)realloc(a->items, ncap * sizeof(void *));
    if (!nitems) {
        return;
    }
    a->items = nitems;
    a->cap = ncap;
}

set_t *set_create(set_event_fn on_insert, set_event_fn on_remove) {
    set_t *s = (set_t *)malloc(sizeof(*s));
    if (!s) return NULL;
    set_init(s, on_insert, on_remove);
    return s;
}

void set_init(set_t *a, set_event_fn on_insert, set_event_fn on_remove) {
    a->used = 0;
    a->cap = 0;
    a->last = 0;
    a->items = NULL;
    a->on_insert = on_insert;
    a->on_remove = on_remove;
}

int set_append(set_t *a, void *item) {
    if (a->used >= a->cap) set_grow(a);
    if (a->used >= a->cap) {
        return -1;
    }

    a->items[a->used++] = item;
    if (a->on_insert) {
        a->on_insert(a, item, a->used - 1);
    }
    return 0;
}

int set_delete(set_t *a, size_t i) {
    void *item;

    if (i >= a->used) return -1;
    item = a->items[i];
    a->items[i] = a->items[--a->used];

    /* it has already been removed now */
    if (a->on_remove) {
        a->on_remove(a, item, i);
    }
    return 0;
}

void set_clear(set_t *a) {
    while (set_delete(a, 0) == 0) { /* do nothing */ };
    free(a->items);
    set_init(a, a->on_insert, a->on_remove);
}

int set_remove(set_t *a, void *item) {
    size_t i;
    for (i = 0; i < a->used; ++i) {
        if (a->items[i] == item) {
            return set_delete(a, i);
        }
    }
    return -1;
}

int set_contains(set_t *a, void *item) {
    size_t i;
    for (i = 0; i < a->used; ++i) {
        if (a->items[i] == item) {
            return 1;
        }
    }
    return 0;
}

void *set_take(set_t *a) {
    void *item;
    if (!a->used) return NULL;
    a->last = a->last % a->used;
    item = a->items[a->last];
    set_delete(a, a->last);
    ++a->last;
    return item;
}

void set_destroy(set_t *a) {
    while (set_delete(a, 0) == 0) { /* do nothing */ };
    free(a->items);
    a->used = 0;
    a->cap = 0;
    a->last = 0;
    a->on_insert = NULL;
    a->on_remove = NULL;
}

void set_free(set_t *a) {
    set_destroy(a);
    free(a);
}

#ifdef SET_TEST_MAIN
#include <stdio.h>
#include <assert.h>

void on_insert(set_t *s, void *item, size_t i) {
    printf("insert %ld at %d\n", (long)item, i);
}

void on_remove(set_t *s, void *item, size_t i) {
    printf("remove %ld from %d\n", (long)item, i);
}

int main(int argc, char **argv) {
    intptr_t i = 0;
    void *ptr;
    set_t *s = set_create(on_insert, on_remove);
    assert(s);

    for (i = 0; i < 10000; ++i) {
        set_append(s, (void *)i);
    }
    
    while (s->used != 0) {
        set_take(s);
    }

    set_free(s);
    exit(0);
}
#endif /* SET_TEST_MAIN */
