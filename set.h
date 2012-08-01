#ifndef __SET_H_INCLUDED__
#define __SET_H_INCLUDED__

#include <sys/types.h>

typedef struct set set_t;
typedef void (*set_event_fn)(set_t *a, void *item, size_t i);

struct set {
    size_t  used;
    size_t  cap;
    size_t  last;
    void    **items;
    set_event_fn on_insert;
    set_event_fn on_remove;
};

set_t *set_create(set_event_fn on_insert, set_event_fn on_remove);
void set_init(set_t *a, set_event_fn on_insert, set_event_fn on_remove);
void set_free(set_t *a);
void set_destroy(set_t *a);
void set_clear(set_t *a);
int set_append(set_t *a, void *item);
int set_remove(set_t *a, void *item);
int set_delete(set_t *a, size_t i);
int set_contains(set_t *a, void *item);
void *set_take(set_t *a);

#endif /* __SET_H_INCLUDED__ */
