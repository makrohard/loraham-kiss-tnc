#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "rflog.h"
#include "loraham_kiss_tnc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int    g_fd = -1;
static char   g_path[LHKT_PATH_MAX + 8];
static size_t g_max_bytes = LHKT_RFLOG_MAX_BYTES;

int lhkt_rflog_open(const char *path)
{
    int fd;

    /* Absolute only: the controller names the file; this process never
     * resolves a relative path against a directory it did not choose. */
    if (!path || path[0] != '/' || strlen(path) >= sizeof(g_path) - 2) {
        return LHKT_ERR;
    }
    /* O_RDWR, not O_WRONLY: the rollover reads this same descriptor to copy
     * the tail out. O_APPEND still lands every write at the current end. */
    fd = open(path, O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        return LHKT_ERR;
    }
    if (g_fd >= 0) {
        close(g_fd);
    }
    g_fd = fd;
    strcpy(g_path, path);
    return LHKT_OK;
}

void lhkt_rflog_close(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_max_bytes = LHKT_RFLOG_MAX_BYTES;
}

int lhkt_rflog_active(void)
{
    return g_fd >= 0;
}

void lhkt_rflog_set_max_bytes(size_t max_bytes)
{
    g_max_bytes = max_bytes;
}

/* --- Summary: the TNC2 text this boundary already carries --------------- */
const char *lhkt_rflog_tnc2_summary(const uint8_t *packet, size_t len, char *out, size_t out_size)
{
    size_t i;
    size_t n;

    if (!packet || !out || out_size == 0) {
        return NULL;
    }
    if (len < 3 || packet[0] != 0x3c || packet[1] != 0xff || packet[2] != 0x01) {
        return NULL;
    }
    n = len - 3;
    if (n >= out_size) {
        n = out_size - 1;
    }
    for (i = 0; i < n; i++) {
        uint8_t c = packet[3 + i];
        /* Printable only; quote and backslash would break the line's quoting. */
        out[i] = (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? (char)c : '.';
    }
    out[n] = '\0';
    return out;
}

/* --- Formatting ----------------------------------------------------------- */
static void rflog_utc_now(char *out, size_t out_size)
{
    struct timespec ts;
    struct tm tm;
    char base[32];

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm);
    snprintf(out, out_size, "%s.%03dZ", base, (int)(ts.tv_nsec / 1000000L));
}

static size_t rflog_put(char *out, size_t out_size, size_t pos, const char *text)
{
    int n = snprintf(out + pos, pos < out_size ? out_size - pos : 0, "%s", text);
    return n < 0 ? pos : pos + (size_t)n;
}

/* tnc2="..." (when the marker is present) hex=<..> ascii="<..>"\n — bounded. */
static size_t rflog_append_tail(char *out, size_t out_size, size_t pos,
                                const uint8_t *packet, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    char tnc2[260];
    size_t i;

    if (lhkt_rflog_tnc2_summary(packet, len, tnc2, sizeof(tnc2))) {
        pos = rflog_put(out, out_size, pos, " tnc2=\"");
        pos = rflog_put(out, out_size, pos, tnc2);
        pos = rflog_put(out, out_size, pos, "\"");
    }
    pos = rflog_put(out, out_size, pos, " hex=");
    for (i = 0; i < len && pos + 2 < out_size; i++) {
        out[pos++] = digits[packet[i] >> 4];
        out[pos++] = digits[packet[i] & 0x0f];
    }
    pos = rflog_put(out, out_size, pos, " ascii=\"");
    for (i = 0; i < len && pos + 1 < out_size; i++) {
        uint8_t c = packet[i];
        out[pos++] = (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? (char)c : '.';
    }
    pos = rflog_put(out, out_size, pos, "\"\n");
    if (pos >= out_size) {
        /* Truncated: still end the record, so the file stays one per line. */
        out[out_size - 2] = '\n';
        out[out_size - 1] = '\0';
        return out_size - 1;
    }
    out[pos] = '\0';
    return pos;
}

size_t lhkt_rflog_format_rx(char *out, size_t out_size, const char *utc,
                            int16_t rssi_cdbm, int16_t snr_cdb,
                            const uint8_t *packet, size_t len)
{
    int n;

    if (!out || out_size < 8) {
        return 0;
    }
    n = snprintf(out, out_size, "%s RX rssi=%.2f snr=%.2f len=%zu",
                 utc ? utc : "-", rssi_cdbm / 100.0, snr_cdb / 100.0, len);
    if (n < 0) {
        return 0;
    }
    return rflog_append_tail(out, out_size, (size_t)n < out_size ? (size_t)n : out_size - 1,
                             packet, len);
}

size_t lhkt_rflog_format_tx(char *out, size_t out_size, const char *utc, const char *outcome,
                            const uint8_t *packet, size_t len)
{
    int n;

    if (!out || out_size < 8) {
        return 0;
    }
    n = snprintf(out, out_size, "%s TX rssi=- snr=- len=%zu outcome=%s",
                 utc ? utc : "-", len, outcome ? outcome : "-");
    if (n < 0) {
        return 0;
    }
    return rflog_append_tail(out, out_size, (size_t)n < out_size ? (size_t)n : out_size - 1,
                             packet, len);
}

/* --- Retention: copy-truncate, same inode --------------------------------- */
static int rflog_copy_to_previous(void)
{
    char prev[sizeof(g_path) + 8];
    uint8_t chunk[65536];
    off_t off = 0;
    int pfd;
    int ok = 1;

    snprintf(prev, sizeof(prev), "%s.1", g_path);
    pfd = open(prev, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (pfd < 0) {
        return 0;
    }
    for (;;) {
        ssize_t n = pread(g_fd, chunk, sizeof(chunk), off);
        ssize_t done = 0;
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = 0;
            break;
        }
        if (n == 0) {
            break;
        }
        while (done < n) {
            ssize_t w = write(pfd, chunk + done, (size_t)(n - done));
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ok = 0;
                break;
            }
            done += w;
        }
        if (!ok) {
            break;
        }
        off += n;
    }
    close(pfd);
    return ok;
}

static void rflog_write_line(const char *line, size_t n)
{
    struct stat st;
    size_t done = 0;

    if (g_fd < 0) {
        return;
    }
    if (fstat(g_fd, &st) == 0 && (size_t)st.st_size + n > g_max_bytes) {
        /* The previous segment is replaced only when the copy succeeded; a
         * failed copy keeps the live file intact and lets it grow past the cap. */
        if (rflog_copy_to_previous() && ftruncate(g_fd, 0) != 0) {
            /* Copied but not truncated: the live file keeps growing and the
             * next roll overwrites <path>.1 again. Nothing is lost either way. */
        }
    }
    while (done < n) {
        ssize_t w = write(g_fd, line + done, n - done);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;         /* a lost line is not worth blocking the bridge */
        }
        done += (size_t)w;
    }
}

/* 255-byte packet: 510 hex + 255 ascii + 255 tnc2 + fixed fields < 1280. */
#define LHKT_RFLOG_LINE_MAX 1280

void lhkt_rflog_rx(int16_t rssi_cdbm, int16_t snr_cdb, const uint8_t *packet, size_t len)
{
    char utc[40];
    char line[LHKT_RFLOG_LINE_MAX];

    if (g_fd < 0) {
        return;
    }
    rflog_utc_now(utc, sizeof(utc));
    rflog_write_line(line, lhkt_rflog_format_rx(line, sizeof(line), utc, rssi_cdbm, snr_cdb,
                                                packet, len));
}

void lhkt_rflog_tx(const char *outcome, const uint8_t *packet, size_t len)
{
    char utc[40];
    char line[LHKT_RFLOG_LINE_MAX];

    if (g_fd < 0) {
        return;
    }
    rflog_utc_now(utc, sizeof(utc));
    rflog_write_line(line, lhkt_rflog_format_tx(line, sizeof(line), utc, outcome, packet, len));
}
