#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "times.h"
#include "event.h"

#ifndef EPOLLRDHUP
#define EPOLLRDHUP  0x2000
#endif /* EPOLLRDHUP */

static void handle(evtent_t *ent, int evset) {
    int c = 0;

    if (evset & (EPOLLHUP | EPOLLRDHUP)) {
        c = EVENT_HUP;
    } else if (evset & EPOLLIN) {
        c = EVENT_RD;
    } else if (evset & EPOLLOUT) {
        c = EVENT_WR;
    }
    ent->f(ent->x, c);
}

/* interval in unit of minisecond(ms) */
event_t *event_create(handle_fn tick, void *tickval, int interval) {
    event_t *evt = (event_t*)calloc(1, sizeof(*evt));
    if (event_init(evt, tick, tickval, interval) != 0) {
        free(evt);
        return NULL;
    }
    return evt;
}

int event_init(event_t *evt, handle_fn tick, void *tickval, int interval) {
    evt->stop = 0;
    evt->tick = tick;
    evt->tickval = tickval;
    evt->interval = interval;
    evt->fd_count = 0;
    evt->epoll_fd = epoll_create(1);
    if (evt->epoll_fd == -1) {
        return -1;
    }
    return 0;
}

int event_regis(event_t *evt, evtent_t *ent, int rwd) {
    int op;
    struct epoll_event ev = {};
    assert(evt && ent);

    if (!ent->added && rwd == EVENT_DEL) {
        return -1;
    } else if (!ent->added && rwd != EVENT_DEL) {
        ent->added = 1;
        op = EPOLL_CTL_ADD;
        ++evt->fd_count;
    } else if (rwd == EVENT_DEL) {
        op = EPOLL_CTL_DEL;
        --evt->fd_count;
    } else {
        op = EPOLL_CTL_MOD;
    }

    switch (rwd) {
    case EVENT_RD:
        ev.events = EPOLLIN;
        break;
    case EVENT_WR:
        ev.events = EPOLLOUT;
        break;
    }
    ev.events |= EPOLLRDHUP | EPOLLPRI;
    ev.data.ptr = ent;

    return epoll_ctl(evt->epoll_fd, op, ent->fd, &ev);
}

void event_loop(event_t *evt) {
    int i, r;
    long long e, t = ustime();
    struct epoll_event evs[512];

    while (!evt->stop) {
        r = epoll_wait(evt->epoll_fd, evs, 512, evt->interval);

        if (r < 0 && errno != EINTR) {
            fprintf(stderr, "epoll_wait failed:%s\n", strerror(errno));
            exit(1);
        }

        e = ustime();
        if ((e - t) / 1000 >= evt->interval) {
            evt->tick(evt->tickval, EVENT_TICK);
            t = e;
        }

        for (i = 0; i < r; ++i) {
            handle(evs[i].data.ptr, evs[i].events);
        }
    }
}

void event_destroy(event_t *evt) {
    if (evt->epoll_fd >= 0) {
        close(evt->epoll_fd);
        evt->epoll_fd = -1;
    }
    evt->stop = 0;
    evt->tick = NULL;
    evt->tickval = NULL;
    evt->interval = 0;
    evt->fd_count = 0;
}

void event_free(event_t *evt) {
    event_destroy(evt);
    free(evt);
}

void event_stop(event_t *evt) {
    evt->stop = 1;
}

evtent_t *event_entry_create(handle_fn handler, void *arg, int fd) {
    evtent_t *ent = malloc(sizeof(*ent));
    ent->fd = fd;
    ent->f = handler;
    ent->x = arg;
    ent->added = 0;
    return ent;
}

void event_entry_free(evtent_t *ent) {
    free(ent);
}

#ifdef EVENT_TEST_MAIN
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "net.h"

static void tick(void *arg, int noused) {
    static int count = 0;
    printf("%lu\n", count++);
}

static void accept_process(void *arg, int event) {
    int sock;
    char *msg = "Hello Event\n";
    evtent_t *ent = (evtent_t *)arg;
    struct sockaddr_in sockaddr;
    socklen_t socklen = sizeof(sockaddr);
    memset(&sockaddr, 0, socklen);

    switch (event) {
    case EVENT_WR:
    case EVENT_RD:
        if ((sock = accept(ent->fd,
                        (struct sockaddr *)&sockaddr, &socklen)) != -1) {
            printf("Received a connection\n");
            write(sock, msg, strlen(msg));
            close(sock);
        }
        break;
    }
}

int main(int argc, char **argv) {
    int count;
    int sockfds[1];
    int maxfd;

    evtent_t *listen_sock;
    event_t *evt = event_create(tick, NULL, 1000);  /* 1000 ms */
    assert(evt);

    if ((count = create_socket("localhost", 5986, sockfds, 
            1, &maxfd)) == 0) {
        fprintf(stderr, "create_socket failed\n");
        exit(1);
    }
    printf("%d fds was created\n", count);

    listen_sock = event_entry_create(accept_process, NULL, sockfds[0]);
    listen_sock->x = listen_sock;
    assert(listen_sock);

    assert(event_regis(evt, listen_sock, EVENT_RD) == 0);
    event_loop(evt);
    exit(0);

}
#endif /* EVENT_TEST_MAIN */
