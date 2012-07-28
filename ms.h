#ifndef __MS_H_INCLUDED__
#define __MS_H_INCLUDED__

typedef void (*ms_event_fn)(ms *a, void *item, size_t i);

typedef struct ms {
    size_t  used;
    size_t  cap;
    size_t  last;
    void    **items;
    ms_event_fn on_insert;
    ms_event_fn on_remove;
} ms;

void ms_init(ms *a, ms_event_fn on_insert, ms_event_fn on_remove);
void ms_clear(ms *a);
int ms_append(ms *a, void *item);
int ms_remove(ms *a, void *item);
int ms_delete(ms *a, size_t i);
int ms_contains(ms *a, void *item);
void *ms_take(ms *a);

#endif /* __MS_H_INCLUDED__ */
