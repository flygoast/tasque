#include <stdio.h>
#include <sys/time.h>

/* Return the UNIX time in milliseconds */
long long mstime() {
    struct timeval tv = {};
    long long mst = 0;
    gettimeofday(&tv, NULL);
    mst = ((long long)tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return mst;
}

/* Return the UNIX time in microseconds */
long long ustime() {
    struct timeval tv = {};
    long long ust = 0;
    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1000000 + tv.tv_usec;
    return ust;
}

#ifdef TIMES_TEST_MAIN
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("Second:%d, Millisecond:%lld, Microsecond:%lld\n",
        time(NULL), mstime(), ustime());
    exit(0);
}
#endif /* TIMES_TEST_MAIN */
