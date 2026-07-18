#ifndef LORAHAM_KISS_TNC_H
#define LORAHAM_KISS_TNC_H

#include <stddef.h>
#include <stdint.h>

/* Gemeinsame Limits */
#define LHKT_AX25_MAX_REPEATERS 8
#define LHKT_AX25_MAX_ADDRS     10
#define LHKT_AX25_MAX_PAYLOAD   256
#define LHKT_AX25_MAX_FRAME     384
#define LHKT_TNC2_MAX_LINE      512
#define LHKT_KISS_MAX_FRAME     1024
#define LHKT_LORAHAM_TX_MAX     255
#define LHKT_LORAHAM_HDR_LEN    3
#define LHKT_PATH_MAX           256
#define LHKT_HOST_MAX           64

/* Defaults */
#define LHKT_DEFAULT_KISS_HOST   "127.0.0.1"

/* Default source allow-list: loopback only (127.0.0.1/32). */
#define LHKT_DEFAULT_BIND        "127.0.0.1"
#define LHKT_DEFAULT_KISS_PORT   8001
#define LHKT_DEFAULT_DATA_SOCKET "/tmp/lora433f.sock"
#define LHKT_DEFAULT_CONF_SOCKET "/tmp/loraconf433.sock"
/* Fehlercodes */
#define LHKT_OK                 0
#define LHKT_ERR               -1
#define LHKT_ERR_SHORT         -2
#define LHKT_ERR_LONG          -3
#define LHKT_ERR_FORMAT        -4
#define LHKT_ERR_UNSUPPORTED   -5
#define LHKT_ERR_NOSPACE       -6
#define LHKT_ERR_TX_SOCKET     -7
#define LHKT_ERR_CLIENT_SOCKET -8
#define LHKT_ERR_CONF_SOCKET   -9
#define LHKT_ERR_TX_RESULT    -10

typedef struct {
    char kiss_host[LHKT_HOST_MAX];   /* listen bind address (derived from bind,
                                      * or an explicit --kiss-host override) */
    int  kiss_port;

    /* Source-IP allow-list (--bind CIDR). A peer may connect only if its
     * address is inside this network. */
    char     bind_spec[LHKT_HOST_MAX];  /* CIDR text for display, e.g. 10.0.0.0/24 */
    uint32_t bind_net;                  /* network address, host byte order, masked */
    int      bind_prefix;               /* 0..32 */

    char data_socket[LHKT_PATH_MAX];
    char conf_socket[LHKT_PATH_MAX];

    int rx_only;
    int verbose;
    int stats_interval;
    int tx_settle_ms;
    int tx_return_ms;
    int tx_busy_timeout_ms;
    int tx_queue_len;
    int tx_packet_ttl_ms;


    double rx_freq;
    double tx_freq;
    int have_rx_freq;
    int have_tx_freq;

    int sf;
    double bw;
    int cr;
    int crc;
    int preamble;
    int syncword;
    int ldro;
    int ldro_auto;
    int power;
} lhkt_config_t;

typedef struct {
    uint64_t kiss_rx;
    uint64_t kiss_tx;
    uint64_t kiss_drop;

    uint64_t ax25_rx;
    uint64_t ax25_tx;
    uint64_t ax25_drop;

    uint64_t tnc2_rx;
    uint64_t tnc2_tx;
    uint64_t tnc2_drop;

    uint64_t loraham_rx;
    uint64_t loraham_tx;
    uint64_t loraham_drop;
    uint64_t loraham_framed_errors;

    uint64_t cad_busy_events;
    uint64_t cad_idle_events;
    int cad_current_busy;

    uint64_t tx_drop_oversize;
    uint64_t tx_unconfirmed;
    uint64_t tx_restore_failures;
    uint64_t socket_reconnects;
    uint64_t client_disconnects;
} lhkt_stats_t;

void lhkt_config_defaults(lhkt_config_t *cfg);

/* Parse a --bind / config `bind` argument: an IPv4 address ("A.B.C.D",
 * implicit /32) or CIDR ("A.B.C.D/N", N=0..32). Sets cfg->bind_net/prefix/spec
 * (the source allow-list) and derives cfg->kiss_host (the listen address:
 * loopback when the network is within 127.0.0.0/8, else 0.0.0.0). Returns
 * LHKT_OK or LHKT_ERR_FORMAT. */
int  lhkt_bind_apply(lhkt_config_t *cfg, const char *arg);

/* True if the host-order IPv4 address is inside net_host/prefix. */
int  lhkt_ipv4_in_cidr(uint32_t addr_host, uint32_t net_host, int prefix);

void lhkt_stats_init(lhkt_stats_t *stats);
void lhkt_stats_print(const lhkt_stats_t *stats);

#endif
