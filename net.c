#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "net.h"

int create_socket(const char *ifname, int port, 
        int sockfds[], size_t sz, int *pmaxfd) {
    struct addrinfo hints, *res, *res0;
    int error;
    int count = 0;
    int on = 1;
    int sockfd;
    int maxfd = -1;
    char portbuf[10];

    snprintf(portbuf, sizeof(portbuf), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    error = getaddrinfo(ifname, portbuf, &hints, &res0);
    if (error) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }

    count = 0;
    maxfd = -1;
    for (res = res0; res; res = res->ai_next) {
        if ((sockfd = socket(res->ai_family, res->ai_socktype,
                res->ai_protocol)) < 0) {
            perror("socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            &on, sizeof(on))) {
            perror("setsockopt");
            close(sockfd);
            continue;
        }

        if (bind(sockfd, res->ai_addr, res->ai_addrlen)) {
            perror("bind");
            close(sockfd);
            continue;
        }

        if (listen(sockfd, SOMAXCONN)) {
            perror("listen");
            close(sockfd);
            continue;
        }

        sockfds[count++] = sockfd;
        if (sockfd > maxfd) {
            maxfd = sockfd;
        }

        if (count >= sz) {
            break;
        }
    }
    freeaddrinfo(res0);
    if (pmaxfd) {
        *pmaxfd = maxfd;
    }
    return count;
}

#ifdef NET_TEST_MAIN
#include <sys/select.h>
#include <errno.h>

static char *msg = "hello world\r\n";

int main(int argc, char *argv[]) {
    int count;
    int sockfds[10];
    int maxfd;
    fd_set  rfds;
    int i;
    struct timeval timeout;
    int ready;

    if ((count = create_socket("localhost", 5986, sockfds, 
            10, &maxfd)) == 0) {
        fprintf(stderr, "create_socket failed\n");
        exit(1);
    }
    printf("%d fds was created\n", count);
    while (1) {
        FD_ZERO(&rfds);
        for (i = 0; i < count; ++i) {
            FD_SET(sockfds[i], &rfds);
        }
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        if ((ready = select(maxfd + 1, &rfds, NULL, NULL, &timeout)) 
                == -1 && errno != EINTR) {
            perror("select");
            exit(1);
        }

        if (ready > 0) {
            for (i = 0; i < count; ++i) {
                if (FD_ISSET(sockfds[i], &rfds)) {
                    int sock;
                    struct sockaddr_in sockaddr;
                    socklen_t socklen;

                    socklen = sizeof(sockaddr);
                    memset(&sockaddr, 0, socklen);
                    if ((sock = accept(sockfds[i], 
                        (struct sockaddr *)&sockaddr, &socklen)) != -1) {
                        write(sock, msg, strlen(msg));
                        close(sock);
                    }
                }
            }
        }
    }
    exit(0);
}

static int net_generic_accept(int sockfd, struct sockaddr *sa, 
        socklen_t *len) {
    int fd;
    while (1) {
        fd = accept(sockfd, sa, len);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }
        break;
    }
    return fd;
}

int tcp_accept(int sockfd, char *ip, int *port) {
    int fd;
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);
    if ((fd = anet_generic_accept(err, sockfd, 
            (struct sockaddr*)&sa, &salen)) == -1) {
        return -1;
    }

    if (ip) {
        strcpy(ip, inet_ntoa(sa.sin_addr));
    }

    if (port) {
        *port = ntohs(sa.sin_port);
    }
    return fd;
}

int net_nonblock(int fd) {
    int flags;

    /* Set the socket nonblocking. Note that fcntl(2) for F_GETFL
       and F_SETFL can't be interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }
    return 0;
}

#endif /* NET_TEST_MAIN */
