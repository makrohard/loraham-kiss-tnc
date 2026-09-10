#include "tcp_server.h"
#include "loraham_kiss_tnc.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Small IPv4 TCP server for one KISS/TCP client.
 * Multi-client handling is intentionally out of scope.
 */

int lhkt_tcp_server_listen(const char *host, int port)
{
    int fd;
    int yes;
    int flags;
    struct sockaddr_in addr;

    if (!host || host[0] == '\0' || port < 1 || port > 65535) {
        return LHKT_ERR_FORMAT;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return LHKT_ERR;
    }

    yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return LHKT_ERR_FORMAT;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    if (listen(fd, 1) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    /* Non-blocking: accept() is only entered after select() reports the
     * listen fd readable, but a rejected peer must not block the single
     * event loop waiting for the next connection on an empty backlog. */
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return LHKT_ERR;
    }

    return fd;
}

int lhkt_tcp_server_accept(int listen_fd, uint32_t allow_net, int allow_prefix,
                           char *peer, size_t peer_size)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t addr_len;
    char ip[INET_ADDRSTRLEN];

    if (listen_fd < 0) {
        return LHKT_ERR;
    }

    /* Exactly one accept(): the caller (select loop) owns the retry. A
     * transient condition or an allow-list rejection returns LHKT_ERR_AGAIN so
     * the loop simply re-selects rather than blocking on an empty backlog. */
    addr_len = sizeof(addr);
    fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK ||
            errno == ECONNABORTED || errno == EINTR) {
            return LHKT_ERR_AGAIN;
        }
        return LHKT_ERR;
    }

    /* Source allow-list: reject and hand control back to the select loop. */
    if (!lhkt_ipv4_in_cidr(ntohl(addr.sin_addr.s_addr),
                           allow_net, allow_prefix)) {
        if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
            fprintf(stderr,
                    "[KISS] rejected connection from %s (not in allow-list)\n",
                    ip);
        }
        close(fd);
        return LHKT_ERR_AGAIN;
    }

    if (peer && peer_size > 0) {
        if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
            snprintf(peer, peer_size, "%s:%u", ip,
                     (unsigned int)ntohs(addr.sin_port));
        } else {
            snprintf(peer, peer_size, "unknown");
        }
    }

    return fd;
}

void lhkt_tcp_server_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}
