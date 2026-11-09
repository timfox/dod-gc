#ifndef TEST_NETWORK_MOCK_H
#define TEST_NETWORK_MOCK_H
#include <stdint.h>
#include <stddef.h>
#include <sys/select.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef unsigned socklen_t;
#define AF_INET 2
#define AF_UNSPEC 0
#define MSG_DONTWAIT 0x40
struct in_addr { u32 s_addr; };
struct sockaddr { u8 sa_len, sa_family; char data[14]; };
struct sockaddr_in { u8 sin_len, sin_family; u16 sin_port; struct in_addr sin_addr; char zero[8]; };
int net_socket(unsigned, unsigned, unsigned);
int net_bind(int, struct sockaddr *, socklen_t);
int net_connect(int, struct sockaddr *, socklen_t);
int net_recvfrom(int, void *, size_t, unsigned, struct sockaddr *, socklen_t *);
int net_close(int);
int net_setsockopt(int, int, int, const void *, socklen_t);
int net_getsockopt(int, int, int, void *, socklen_t *);
int net_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int net_ioctl(int, unsigned long, void *);
int net_getsockname(int, struct sockaddr *, socklen_t *);
int net_send(int, const void *, size_t, int);
int net_recv(int, void *, size_t, int);
int net_listen(int, int);
int net_accept(int, struct sockaddr *, socklen_t *);
#endif
