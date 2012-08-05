#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "srv.h"

static void enter_drain_mode(int sig) {
    tasque_srv.drain_mode = 1;
}

static void set_sig_handlers() {
    int ret;
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    ret = sigemptyset(&sa.sa_mask);
    if (ret < 0) {
        fprintf(stderr, "sigemtpyset failed:%s\n", strerror(errno));
        exit(1);
    }
    ret = sigaction(SIGPIPE, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "sigaction(SIGPIPE) failed:%s\n", strerror(errno));
        exit(1);
    }

    sa.sa_handler = enter_drain_mode;
    ret = sigaction(SIGUSR1, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "sigaction(SIGUSR1) failed:%s\n", strerror(errno));
        exit(1);
    }
}

static void option_parse(int argc, char **argv) {
    tasque_srv.verbose = 3;
}

int main(int argc, char **argv) {
    setlinebuf(stdout);
    set_sig_handlers();
    srv_init();
    option_parse(argc, argv);
    srv_serve();
    srv_destroy();
    exit(0);
}
