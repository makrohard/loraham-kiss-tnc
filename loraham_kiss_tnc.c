#define _POSIX_C_SOURCE 200809L

#include "loraham_kiss_tnc.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>

/* systemd deployments serve the daemon sockets under /run/loraham, direct/user starts under /tmp
 * (LORAHAM_SOCKET_DIR). Take the path where the daemon socket actually exists, else the /tmp
 * fallback — mirrors the daemon's clients (lorachat/igate). Only the DEFAULT; an explicit
 * --data-socket/--conf-socket (or config-file key) still overrides. */
static const char *loraham_sockpath(const char *runp, const char *tmpp)
{
    struct stat st;
    return (stat(runp, &st) == 0 && S_ISSOCK(st.st_mode)) ? runp : tmpp;
}

void lhkt_config_defaults(lhkt_config_t *cfg)
{
    if (!cfg) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));

    cfg->kiss_port = LHKT_DEFAULT_KISS_PORT;
    lhkt_bind_apply(cfg, LHKT_DEFAULT_BIND);   /* 127.0.0.1/32, loopback only */

    snprintf(cfg->data_socket, sizeof(cfg->data_socket), "%s",
             loraham_sockpath("/run/loraham/lora433f.sock", LHKT_DEFAULT_DATA_SOCKET));
    snprintf(cfg->conf_socket, sizeof(cfg->conf_socket), "%s",
             loraham_sockpath("/run/loraham/loraconf433.sock", LHKT_DEFAULT_CONF_SOCKET));

    cfg->rx_only = 0;
    cfg->verbose = 0;
    cfg->stats_interval = 900;
    cfg->tx_settle_ms = 100;
    cfg->tx_return_ms = 1000;
    cfg->tx_busy_timeout_ms = 120000;
    cfg->tx_queue_len = 8;
    cfg->tx_packet_ttl_ms = 180000;


    cfg->rx_freq = 433.775;
    cfg->tx_freq = 433.900;
    cfg->have_rx_freq = 1;
    cfg->have_tx_freq = 1;

    cfg->sf = 12;
    cfg->bw = 125.0;
    cfg->cr = 5;
    cfg->crc = 1;
    cfg->preamble = 8;
    cfg->syncword = 0x12;
    cfg->ldro = 1;
    cfg->ldro_auto = 0;
    cfg->power = 17;
}

void lhkt_stats_init(lhkt_stats_t *stats)
{
    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
}

void lhkt_stats_print(const lhkt_stats_t *stats)
{
    if (!stats) {
        return;
    }

    printf("[STAT] KISS rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 "\n",
           stats->kiss_rx, stats->kiss_tx, stats->kiss_drop);
    printf("[STAT] AX25 rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 "\n",
           stats->ax25_rx, stats->ax25_tx, stats->ax25_drop);
    printf("[STAT] TNC2 rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 "\n",
           stats->tnc2_rx, stats->tnc2_tx, stats->tnc2_drop);
    printf("[STAT] LoRaHAM rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64 " framed_errors=%" PRIu64 "\n",
           stats->loraham_rx,
           stats->loraham_tx,
           stats->loraham_drop,
           stats->loraham_framed_errors);
    printf("[STAT] CAD current_busy=%d busy_events=%" PRIu64 " idle_events=%" PRIu64 "\n",
           stats->cad_current_busy,
           stats->cad_busy_events,
           stats->cad_idle_events);
    printf("[STAT] TX oversize=%" PRIu64 " unconfirmed=%" PRIu64 " restore_failures=%" PRIu64 " reconnects=%" PRIu64 " client_disconnects=%" PRIu64 "\n",
           stats->tx_drop_oversize,
           stats->tx_unconfirmed,
           stats->tx_restore_failures,
           stats->socket_reconnects,
           stats->client_disconnects);
}

/* --- KISS/TCP source allow-list (--bind CIDR) ---------------------------- */

int lhkt_ipv4_in_cidr(uint32_t addr_host, uint32_t net_host, int prefix)
{
    uint32_t mask;

    if (prefix <= 0) {
        return 1;                       /* /0 matches everything */
    }
    if (prefix >= 32) {
        return addr_host == net_host;
    }
    mask = 0xFFFFFFFFu << (32 - prefix);
    return (addr_host & mask) == (net_host & mask);
}

int lhkt_bind_apply(lhkt_config_t *cfg, const char *arg)
{
    char buf[LHKT_HOST_MAX];
    char *slash;
    int prefix = 32;
    struct in_addr ina;
    uint32_t ip_host;
    uint32_t mask;
    uint32_t net;

    if (!cfg || !arg || arg[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }
    if (strlen(arg) >= sizeof(buf)) {
        return LHKT_ERR_FORMAT;
    }
    snprintf(buf, sizeof(buf), "%s", arg);

    slash = strchr(buf, '/');
    if (slash) {
        char *end = NULL;
        long p;

        *slash = '\0';
        p = strtol(slash + 1, &end, 10);
        /* Reject an empty prefix ("1.2.3.4/"): strtol returns 0 with
         * end==start, which would otherwise silently mean /0 (allow-all). */
        if (!end || end == slash + 1 || *end != '\0' || p < 0 || p > 32) {
            return LHKT_ERR_FORMAT;
        }
        prefix = (int)p;
    }

    if (inet_pton(AF_INET, buf, &ina) != 1) {
        return LHKT_ERR_FORMAT;
    }

    ip_host = ntohl(ina.s_addr);
    mask = (prefix == 0) ? 0u : (0xFFFFFFFFu << (32 - prefix));
    net = ip_host & mask;

    cfg->bind_net = net;
    cfg->bind_prefix = prefix;
    snprintf(cfg->bind_spec, sizeof(cfg->bind_spec), "%u.%u.%u.%u/%d",
             (net >> 24) & 0xFF, (net >> 16) & 0xFF,
             (net >> 8) & 0xFF, net & 0xFF, prefix);

    /* Derive the listen address: bind loopback only when the whole allowed
     * network is inside 127.0.0.0/8 (so the port is not exposed on other
     * interfaces); otherwise listen on all interfaces and filter on accept. */
    if (((net >> 24) & 0xFF) == 127 && prefix >= 8) {
        snprintf(cfg->kiss_host, sizeof(cfg->kiss_host), "127.0.0.1");
    } else {
        snprintf(cfg->kiss_host, sizeof(cfg->kiss_host), "0.0.0.0");
    }

    return LHKT_OK;
}
