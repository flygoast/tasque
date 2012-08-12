#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include "srv.h"
#include "version.h"

#define LLONG_MAX  9223372036854775807LL

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

static void version() {
    printf("tasque: %s, compiled at %s %s\n", 
        TASQUE_VERSION, __DATE__, __TIME__);
}

static void usage() {
    fprintf(stderr, "Usage: %s [OPTIONS]\n", "tasque");
}

/* Convert a string representing an amount of memory into the
   number of bytes, so for instance memtoll("1Gb") will return
   1073741824 that is (1024 * 1024 * 1024). On parsing error,
   if *err is not NULL, it's set to 1, otherwise it's set to 0. */
int64_t memtoll(const char *p, int *err) {
    const char *u;
    char buf[128];
    long mul; /* unit multiplier */
    int64_t val;
    unsigned int digits;

    if (err)
        *err = 0;
    /* Search the first non digit character. */
    u = p;
    if (*u == '-')
        u++;
    while (*u && isdigit(*u))
        u++;
    if (*u == '\0' || *u == ' ' || !strcasecmp(u, "b")) {
        mul = 1;
    } else if (!strcasecmp(u, "k")) {
        mul = 1000;
    } else if (!strcasecmp(u, "kb")) {
        mul = 1024;
    } else if (!strcasecmp(u, "m")) {
        mul = 1000 * 1000;
    } else if (!strcasecmp(u, "mb")) {
        mul = 1024 * 1024;
    } else if (!strcasecmp(u, "g")) {
        mul = 1000L * 1000 * 1000;
    } else if (!strcasecmp(u, "gb")) {
        mul = 1024L * 1024 * 1024;
    } else {
        if (err) 
            *err = 1;
        mul = 1;
    }
    digits = u - p;
    if (digits >= sizeof(buf)) {
        if (err)
            *err = 1;
        return LLONG_MAX;
    }
    memcpy(buf, p, digits);
    buf[digits] = '\0';
    val = strtoll(buf, NULL, 10);
    return val * mul;
}

static void option_parse(int argc, char **argv) {
    char *end;
    int c;
    int err;
    while ((c = getopt(argc, argv, "p:l:z:u:hvV")) != -1) {
        switch (c) {
        case 'p':
            tasque_srv.port = strtol(optarg, &end, 10);
            if (end == optarg || (*end != ' ' && *end != '\0')) {
                usage();
                exit(1);
            }
            break;
        case 'l':
            tasque_srv.host = strdup(optarg);
            break;
        case 'z':
            tasque_srv.job_data_size_limit = memtoll(optarg, &err);
            if (err) {
                usage();
                exit(1);
            }
            break;
        case 'u':
            tasque_srv.user = strdup(optarg);
            break;
        case 'V':
            ++tasque_srv.verbose;
            break;
        case 'v':
            version();
            exit(0);
            break;
        case 'h':
            usage();
            exit(0);
            break;
        default:
            usage();
            exit(1);
        }
    }
 
    if (argc != optind) {
        usage();
        exit(1);
    }
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
