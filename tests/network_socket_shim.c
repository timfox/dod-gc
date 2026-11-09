/* Compiled on the host against an injected libogc-shaped network.h. */
#include <assert.h>
#include "net_gamecube.h"
static int result;
static unsigned receive_flags;
static u16 remembered;
void GC_NetRememberBoundPort(int s, u16 port) { (void)s; remembered = port; }
u16 GC_NetLookupBoundPort(int s) { (void)s; return remembered; }
int net_socket(unsigned a, unsigned b, unsigned c) { (void)a; (void)b; (void)c; return result; }
int net_bind(int s, struct sockaddr *a, socklen_t n) { (void)s; (void)a; (void)n; return result; }
int net_connect(int s, struct sockaddr *a, socklen_t n) { (void)s; (void)a; (void)n; return result; }
int net_recvfrom(int s, void *p, size_t n, unsigned flags, struct sockaddr *a, socklen_t *l) { (void)s; (void)p; (void)n; (void)a; (void)l; receive_flags = flags; return result; }
int net_close(int s) { (void)s; return result; }
int main(void) {
 result = -EWOULDBLOCK;
 assert(GC_RecvFrom(3, NULL, 0, 0, NULL, NULL) == -1);
 assert(errno == EWOULDBLOCK && (receive_flags & MSG_DONTWAIT));
 assert(GC_Socket(AF_INET6, 2, 17) == -1 && errno == EAFNOSUPPORT);
 result = -EMFILE;
 assert(GC_Socket(AF_INET, 2, 17) == -1 && errno == EMFILE);
 struct sockaddr_in addr = {0};
 addr.sin_family = AF_INET; addr.sin_port = 27015;
 result = -EADDRINUSE;
 assert(GC_Bind(3, (struct sockaddr *)&addr, sizeof(addr)) == -1 && errno == EADDRINUSE);
 assert(remembered == 0);
 result = 0;
 assert(GC_Bind(3, (struct sockaddr *)&addr, sizeof(addr)) == 0 && remembered == 27015);
 result = 25;
 assert(GC_RecvFrom(3, NULL, 0, 0, NULL, NULL) == 25);
 result = 0;
 assert(GC_Close(3) == 0 && remembered == 0);
 return 0;
}
