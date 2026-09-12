#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "../rflog.h"
#include "../loraham_kiss_tnc.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* RF log: TNC2 summary, line contract, copy-truncate retention on one inode. */

static int g_ok = 0;
static int g_fail = 0;

static void expect_int(const char *name, long actual, long expected)
{
    if (actual == expected) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s: expected %ld, got %ld\n", name, expected, actual);
    }
}

static void expect_str(const char *name, const char *actual, const char *expected)
{
    if (actual && strcmp(actual, expected) == 0) {
        g_ok++;
        printf("[ OK ] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s:\n  expected %s\n  got      %s\n", name, expected,
               actual ? actual : "(null)");
    }
}

static long file_inode(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_ino : -1;
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static long count_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    long n = 0;
    int c;
    if (!f) {
        return -1;
    }
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            n++;
        }
    }
    fclose(f);
    return n;
}

static void test_tnc2_summary(void)
{
    const uint8_t aprs[] = {0x3c, 0xff, 0x01, 'D', 'L', '1', 'A', 'B', 'C', '>', 'A', 'P', 'R',
                            'S', ':', '!', '"', 0x01};
    const uint8_t raw[] = {0x01, 0x02, 0x03};
    char out[64];

    expect_str("marker present -> TNC2 text, unprintables dotted",
               lhkt_rflog_tnc2_summary(aprs, sizeof(aprs), out, sizeof(out)), "DL1ABC>APRS:!..");
    expect_int("no marker -> no summary",
               lhkt_rflog_tnc2_summary(raw, sizeof(raw), out, sizeof(out)) == NULL, 1);
    expect_int("too short -> no summary",
               lhkt_rflog_tnc2_summary(aprs, 2, out, sizeof(out)) == NULL, 1);
    expect_str("bounded to the output buffer",
               lhkt_rflog_tnc2_summary(aprs, sizeof(aprs), out, 4), "DL1");
}

static void test_line_format(void)
{
    const uint8_t aprs[] = {0x3c, 0xff, 0x01, 'N', '0', 'C', 'A', 'L', 'L', '>', 'A', 'P', ':', 'x'};
    const uint8_t raw[] = {0xaa, 0x22, 0x5c};
    char line[512];
    size_t n;

    n = lhkt_rflog_format_rx(line, sizeof(line), "2026-09-12T16:03:47.412Z", -10450, 725,
                             aprs, sizeof(aprs));
    expect_str("RX line with the TNC2 summary", line,
               "2026-09-12T16:03:47.412Z RX rssi=-104.50 snr=7.25 len=14 tnc2=\"N0CALL>AP:x\""
               " hex=3cff014e3043414c4c3e41503a78 ascii=\"<..N0CALL>AP:x\"\n");
    expect_int("returned length is strlen", (long)n, (long)strlen(line));

    n = lhkt_rflog_format_tx(line, sizeof(line), "2026-09-12T16:03:51.006Z", "unconfirmed",
                             raw, sizeof(raw));
    expect_str("TX line: no signal, an outcome, no tnc2 without the marker", line,
               "2026-09-12T16:03:51.006Z TX rssi=- snr=- len=3 outcome=unconfirmed"
               " hex=aa225c ascii=\"...\"\n");
}

static void test_line_is_bounded(void)
{
    uint8_t packet[255];
    char line[96];
    size_t n;

    memset(packet, 'A', sizeof(packet));
    n = lhkt_rflog_format_rx(line, sizeof(line), "t", 0, 0, packet, sizeof(packet));
    expect_int("bounded length", (long)n, (long)sizeof(line) - 1);
    expect_int("bounded line ends with newline", line[n - 1] == '\n', 1);
    expect_int("bounded line is NUL-terminated", line[n] == 0, 1);
}

static void test_open_rules(void)
{
    expect_int("relative path refused", lhkt_rflog_open("logs/rf.log"), LHKT_ERR);
    expect_int("null refused", lhkt_rflog_open(NULL), LHKT_ERR);
    expect_int("unopenable directory refused", lhkt_rflog_open("/nonexistent-dir-x/rf.log"), LHKT_ERR);
    expect_int("not active", lhkt_rflog_active(), 0);
}

static void test_rollover_keeps_inode(void)
{
    char dir[] = "/tmp/lhkt-rflog-XXXXXX";
    char path[128], prev[136];
    const uint8_t packet[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    long inode_before;
    int fd;
    int i;

    if (!mkdtemp(dir)) {
        expect_int("mkdtemp", 0, 1);
        return;
    }
    snprintf(path, sizeof(path), "%s/rf-kiss.log", dir);
    snprintf(prev, sizeof(prev), "%s.1", path);

    expect_int("open", lhkt_rflog_open(path), LHKT_OK);
    expect_int("active", lhkt_rflog_active(), 1);
    lhkt_rflog_set_max_bytes(2000);
    inode_before = file_inode(path);

    /* ~110 bytes per line; 30 lines cross a 2000-byte cap once. */
    for (i = 0; i < 30; i++) {
        lhkt_rflog_rx(-9000, 500, packet, sizeof(packet));
    }
    expect_int("previous segment exists after the cap", file_size(prev) > 0, 1);
    expect_int("live file was truncated in place, not renamed", file_inode(path), inode_before);
    expect_int("live file continues below the cap", file_size(path) < 2000, 1);
    expect_int("nothing lost: 30 lines across both", count_lines(prev) + count_lines(path), 30);

    /* External truncate (the controller's Clear): the next line lands at the
     * new end of the same inode. */
    fd = open(path, O_WRONLY);
    expect_int("external truncate", ftruncate(fd, 0), 0);
    close(fd);
    lhkt_rflog_tx("ok", packet, 4);
    expect_int("one line after an external truncate", count_lines(path), 1);
    expect_int("same inode after the external truncate", file_inode(path), inode_before);

    lhkt_rflog_close();
    expect_int("closed -> inactive", lhkt_rflog_active(), 0);
    lhkt_rflog_rx(0, 0, packet, 2);          /* must be a silent no-op */
    expect_int("no write while inactive", count_lines(path), 1);
    unlink(path);
    unlink(prev);
    rmdir(dir);
}

int main(void)
{
    test_tnc2_summary();
    test_line_format();
    test_line_is_bounded();
    test_open_rules();
    test_rollover_keeps_inode();
    printf("\n%d ok, %d failed\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
