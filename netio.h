#ifndef __NET_H
#define __NET_H
#include <sys/types.h>

int socket_dial(int domain, int proto, char* local, int local_port, char* server, int port);
int socket_listen(int domain, int proto, char* local, int port);
int socket_nread(int fd, char* buf, size_t count, int prot);
int socket_nwrite(int fd, const char* buf, size_t count, int prot) /* __attribute__((hot)) */;
int socket_nsendfile(int fromfd, int tofd, const char* buf, size_t count) /* __attribute__((hot)) */;

#define NET_SOFTERROR -1
#define NET_HARDERROR -2

#endif /* __NET_H */