#ifndef LHKT_TCP_SERVER_H
#define LHKT_TCP_SERVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int lhkt_tcp_server_listen(const char *host, int port);
/* Accept the next allowed client. Peers whose source IPv4 is outside
 * allow_net/allow_prefix are rejected (closed, logged) and accept continues.
 * allow_prefix<=0 accepts any source. */
int lhkt_tcp_server_accept(int listen_fd, uint32_t allow_net, int allow_prefix,
                           char *peer, size_t peer_size);
void lhkt_tcp_server_close(int fd);

#ifdef __cplusplus
}
#endif

#endif
