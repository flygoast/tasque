#ifndef __EVENT_H_INCLUDED__
#define __EVENT_H_INCLUDED__

#define EVENT_DEL       0
#define EVENT_RD        1
#define EVENT_WR        2
#define EVENT_TICK      3
#define EVENT_HUP       4

typedef void (*handle_fn)(void *arg, int event);

typedef struct event_entry_st {
    int         fd;
    handle_fn   f;
    void        *x;
    int         added;
} evtent_t;

typedef struct event_st {
    int         epoll_fd;
    int         fd_count;
    handle_fn   tick;
    void        *tickval;
    int         interval;
    int         stop;
} event_t;

event_t *event_create(handle_fn tick, void *tickval, int interval);
int event_init(event_t *evt, handle_fn tick, void *tickval, int interval);
int event_regis(event_t *evt, evtent_t *ent, int rwd);
void event_loop(event_t *evt);
void event_destroy(event_t *evt);
void event_free(event_t *evt);
void event_stop(event_t *evt);

evtent_t *event_entry_create(handle_fn handler, void *arg, int fd);
void event_entry_free(evtent_t *ent);

#endif /* __EVENT_H_INCLUDED__ */
