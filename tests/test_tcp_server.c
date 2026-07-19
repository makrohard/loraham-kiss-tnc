#include "tcp_server.h"
#include "loraham_kiss_tnc.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* A blocking-accept regression must FAIL this test, not hang it. */
static void on_alarm(int sig)
{
    const char msg[] = "[FAIL] test_tcp_server: watchdog timeout (accept blocked?)\n";
    ssize_t w;

    (void)sig;
    w = write(2, msg, sizeof(msg) - 1);
    (void)w;
    _exit(2);
}

/* Listen on 127.0.0.1, trying a few fixed high ports; sets *port on success. */
static int listen_retry(int *port)
{
    static const int ports[] = { 45871, 45872, 45873, 45874, 45875 };
    size_t i;

    for (i = 0; i < sizeof(ports) / sizeof(ports[0]); i++) {
        int fd = lhkt_tcp_server_listen("127.0.0.1", ports[i]);
        if (fd >= 0) {
            *port = ports[i];
            return fd;
        }
    }
    return -1;
}

/* Open a client connection to 127.0.0.1:port (caller closes). */
static int connect_client(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;

    assert(fd >= 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    return fd;
}

/* Block (bounded by the watchdog) until the listen fd has a pending peer. */
static void wait_pending(int listen_fd)
{
    fd_set r;

    for (;;) {
        FD_ZERO(&r);
        FD_SET(listen_fd, &r);
        if (select(listen_fd + 1, &r, NULL, NULL, NULL) > 0) {
            return;
        }
    }
}

int main(void)
{
    int listen_fd;
    int port = 0;
    char peer[64];

    signal(SIGALRM, on_alarm);
    alarm(5);

    listen_fd = listen_retry(&port);
    assert(listen_fd >= 0);

    /* 1) Empty backlog: the non-blocking listen fd returns AGAIN immediately
     *    instead of blocking — this is the core fix. */
    assert(lhkt_tcp_server_accept(listen_fd, 0x7F000001u, 32, peer, sizeof(peer))
           == LHKT_ERR_AGAIN);

    /* 2) A peer outside the allow-list is rejected as AGAIN, and a second
     *    accept on the now-empty backlog also returns AGAIN (never blocks). */
    {
        int c = connect_client(port);

        wait_pending(listen_fd);
        assert(lhkt_tcp_server_accept(listen_fd, 0x0A000000u, 8, peer, sizeof(peer))
               == LHKT_ERR_AGAIN);
        assert(lhkt_tcp_server_accept(listen_fd, 0x0A000000u, 8, peer, sizeof(peer))
               == LHKT_ERR_AGAIN);
        close(c);
    }

    /* 3) An allowed peer yields a valid fd. */
    {
        int c = connect_client(port);
        int a;

        wait_pending(listen_fd);
        a = lhkt_tcp_server_accept(listen_fd, 0x7F000001u, 32, peer, sizeof(peer));
        assert(a >= 0);
        close(a);
        close(c);
    }

    close(listen_fd);
    printf("test_tcp_server: OK\n");
    return 0;
}
