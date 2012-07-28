#ifndef __NET_H_INCLUDED__
#define __NET_H_INCLUDED__

extern int create_socket(const char *ifname, int port, 
        int fds[], size_t sz, int *pmaxfd);

#endif /* __NET_H_INCLUDED__ */
