#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include "srv.h"
#include "conn.h"
#include "tube.h"
#include "net.h"

#define DEFAULT_PORT        8774
#define INIT_TUBE_NUM       8
#define INIT_JOB_NUM        128

/* global server configure and storage */
server_t tasque_srv;

static void srv_su(const char *user) {
    int ret;
    struct passwd   *pwent;

    errno = 0;
    pwent = getpwnam(user);
    if (errno) {
        fprintf(stderr, "getpwname %s failed:%s\n", user, strerror(errno));
        return;
    }

    if (!pwent) {
        fprintf(stderr, "getpwname %s: no such user\n", user);
        return;
    }

    ret = setgid(pwent->pw_gid);
    if (ret == -1) {
        fprintf(stderr, "setgid %d for %s failed:%s\n", pwent->pw_gid,
                user, strerror(errno));
        return;
    }

    ret = setuid(pwent->pw_uid);
    if (ret == -1) {
        fprintf(stderr, "setuid %d for %s failed:%s\n", pwent->pw_uid,
                user, strerror(errno));
        return;
    }
}

void srv_init() {
    tube_t *t = NULL;
    tasque_srv.port = DEFAULT_PORT;
    tasque_srv.host = strdup("0.0.0.0");
    tasque_srv.user = NULL;
    tasque_srv.next_job_id = 1;

    set_init(&tasque_srv.tubes, NULL, NULL);

    t = tube_make_and_insert("default");
    if (!t) {
        fprintf(stderr, "create default tube failed\n");
        exit(1);
    }
    tasque_srv.default_tube = t;
    tube_iref(t);

    if (heap_init(&tasque_srv.conns) != 0) {
        fprintf(stderr, "heap_init failed\n");
        exit(1);
    }

    if (hash_init(&tasque_srv.all_jobs, INIT_JOB_NUM) != 0) {
        fprintf(stderr, "hash_init failed\n");
        exit(1);
    }
    HASH_SET_HASHFN(&tasque_srv.all_jobs, hash_func_int);
}

void srv_serve() {
    int count;
    int sockfds[1];
    int maxfd;
    int ret;

    if (tasque_srv.user) srv_su(tasque_srv.user);

    /* 10 ms */
    if (event_init(&tasque_srv.evt, conn_tick, &tasque_srv, 10) != 0) {
        fprintf(stderr, "event_init failed\n");
        exit(1);
    }

    if ((count = create_socket(tasque_srv.host, tasque_srv.port, 
                sockfds, 1, &maxfd)) == 0) {
        fprintf(stderr, "create_socket failed\n");
        exit(1);
    }

    tasque_srv.sock.x = &tasque_srv.sock;
    tasque_srv.sock.f = (handle_fn)conn_accept;
    tasque_srv.sock.fd = sockfds[0];
    tasque_srv.sock.added = 0;

    ret = event_regis(&tasque_srv.evt, &tasque_srv.sock, EVENT_RD);
    if (ret < 0) {
        fprintf(stderr, "event_regis failed:%s\n", strerror(errno));
        exit(1);
    }

    event_loop(&tasque_srv.evt);
}

void srv_destroy() {
    if (tasque_srv.host) free(tasque_srv.host);
    if (tasque_srv.user) free(tasque_srv.user);

    event_destroy(&tasque_srv.evt);
    heap_destroy(&tasque_srv.conns);
    set_destroy(&tasque_srv.tubes);
    hash_destroy(&tasque_srv.all_jobs);
    dlist_destroy(&tasque_srv.dirty_conns);
}
