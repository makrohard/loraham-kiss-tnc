#include "ax25.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_addr_parse_format(void)
{
    ax25_addr_t addr;
    char out[32];

    assert(ax25_addr_parse("dj0che-10", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "DJ0CHE") == 0);
    assert(addr.ssid == 10);
    assert(addr.repeated == 0);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "DJ0CHE-10") == 0);

    assert(ax25_addr_parse("WIDE1-1*", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "WIDE1") == 0);
    assert(addr.ssid == 1);
    assert(addr.repeated == 1);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "WIDE1-1*") == 0);
}

static void test_addr_accept_boundaries(void)
{
    ax25_addr_t addr;
    char out[32];

    assert(ax25_addr_parse("ABC123", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "ABC123") == 0);
    assert(addr.ssid == 0);
    assert(addr.repeated == 0);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "ABC123") == 0);

    assert(ax25_addr_parse("N0CALL-15", &addr) == LHKT_OK);
    assert(strcmp(addr.call, "N0CALL") == 0);
    assert(addr.ssid == 15);
    assert(addr.repeated == 0);

    assert(ax25_addr_format(&addr, out, sizeof(out)) == LHKT_OK);
    assert(strcmp(out, "N0CALL-15") == 0);
}

static void test_addr_reject_invalid(void)
{
    ax25_addr_t addr;

    assert(ax25_addr_parse("", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("TOOLONG-1", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-16", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-X", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0CHE-*", &addr) == LHKT_ERR_FORMAT);
    assert(ax25_addr_parse("DJ0C/E", &addr) == LHKT_ERR_FORMAT);
}

static void test_encode_decode_roundtrip(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;
    const char *payload = ":DC2EH-11:Hello test{01";

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APLG01", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);
    assert(ax25_addr_parse("WIDE1-1", &in.path[0]) == LHKT_OK);
    assert(ax25_addr_parse("WIDE2-1", &in.path[1]) == LHKT_OK);
    in.path_len = 2;

    memcpy(in.payload, payload, strlen(payload));
    in.payload_len = strlen(payload);

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);
    assert(raw_len > 0);

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_OK);

    assert(strcmp(out.dst.call, "APLG01") == 0);
    assert(out.dst.ssid == 0);

    assert(strcmp(out.src.call, "DJ0CHE") == 0);
    assert(out.src.ssid == 10);

    assert(out.path_len == 2);
    assert(strcmp(out.path[0].call, "WIDE1") == 0);
    assert(out.path[0].ssid == 1);
    assert(strcmp(out.path[1].call, "WIDE2") == 0);
    assert(out.path[1].ssid == 1);

    assert(out.payload_len == strlen(payload));
    assert(memcmp(out.payload, payload, strlen(payload)) == 0);
}

/*
 * Captured from graywolf v0.14.12 over KISS/TCP. A conforming APRS sender sets the
 * command C bit on the destination SSID byte (0xe0) and clears it on the source
 * (0x74). Bit 7 there is NOT a repeated marker and must never become '*'.
 *
 * Only the three SSID bytes carry the bits under test, so the tests below copy this
 * frame and patch offset 6 (dst), 13 (src) or 20 (path) instead of restating it.
 */
#define AX25_SSID_DST   6
#define AX25_SSID_SRC  13
#define AX25_SSID_PATH 20

static const uint8_t graywolf_ui_frame[] = {
    0x82, 0xa0, 0x8e, 0xa4, 0xae, 0x9e, 0xe0,       /* APGRWO,    C=1 */
    0x88, 0x98, 0x62, 0xa8, 0xa6, 0xa8, 0x74,       /* DL1TST-10, C=0 */
    0xae, 0x92, 0x88, 0x8a, 0x62, 0x40, 0x63,       /* WIDE1-1, H=0, last */
    0x03, 0xf0,
    '!', '4', '8', '2', '7', '.', '7', '0', 'N'
};

static void test_decode_command_cbit_is_not_repeated(void)
{
    ax25_frame_t out;

    assert(ax25_decode_ui(graywolf_ui_frame, sizeof(graywolf_ui_frame),
                          &out) == LHKT_OK);

    assert(strcmp(out.dst.call, "APGRWO") == 0);
    assert(out.dst.repeated == 0);
    assert(strcmp(out.src.call, "DL1TST") == 0);
    assert(out.src.ssid == 10);
    assert(out.src.repeated == 0);
    assert(out.path_len == 1);
    assert(strcmp(out.path[0].call, "WIDE1") == 0);
    assert(out.path[0].repeated == 0);
}

/*
 * Bit 7 set on BOTH dst and src — the shape that produced the original
 * "SRC*>DST*" corruption. Neither may be reported as repeated, whatever a peer
 * sets there.
 */
static void test_decode_ignores_bit7_on_both_stations(void)
{
    uint8_t raw[sizeof(graywolf_ui_frame)];
    ax25_frame_t out;

    memcpy(raw, graywolf_ui_frame, sizeof(raw));
    raw[AX25_SSID_DST] |= 0x80;
    raw[AX25_SSID_SRC] |= 0x80;

    assert(ax25_decode_ui(raw, sizeof(raw), &out) == LHKT_OK);
    assert(out.dst.repeated == 0);
    assert(out.src.repeated == 0);
    assert(out.path[0].repeated == 0);
}

/* A set H bit on a path address is still a '*'. */
static void test_decode_path_hbit_is_repeated(void)
{
    uint8_t raw[sizeof(graywolf_ui_frame)];
    ax25_frame_t out;

    memcpy(raw, graywolf_ui_frame, sizeof(raw));
    raw[AX25_SSID_PATH] |= 0x80;

    assert(ax25_decode_ui(raw, sizeof(raw), &out) == LHKT_OK);
    assert(out.path_len == 1);
    assert(out.path[0].repeated == 1);
}

/*
 * Encoding emits the command C bit pair from the frame's POSITION, never from the
 * dst/src repeated flags. Both flag states are checked: with the flags clear the
 * assertions can only hold because bit 7 is hardcoded (this is what fails if the
 * encoder goes back to reading addr->repeated), and with them set they prove the
 * flag is ignored rather than forwarded as an H bit.
 */
static void test_encode_sets_command_cbits(void)
{
    ax25_frame_t in;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;
    int repeated;

    for (repeated = 0; repeated <= 1; repeated++) {
        ax25_frame_init(&in);

        assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
        assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);
        assert(ax25_addr_parse("WIDE1-1*", &in.path[0]) == LHKT_OK);
        in.path_len = 1;
        in.payload[0] = 'x';
        in.payload_len = 1;

        in.dst.repeated = (uint8_t)repeated;
        in.src.repeated = (uint8_t)repeated;

        assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

        assert((raw[AX25_SSID_DST] & 0x80) != 0);    /* dst C=1, always */
        assert((raw[AX25_SSID_SRC] & 0x80) == 0);    /* src C=0, always */
        assert((raw[AX25_SSID_DST] & 0x01) == 0);    /* dst is never the last addr */
        assert((raw[AX25_SSID_PATH] & 0x80) != 0);   /* path H bit preserved */
        assert((raw[AX25_SSID_PATH] & 0x01) != 0);   /* last address */
    }
}

static void test_decode_reject_short_frame(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);
    assert(raw_len > 2);

    assert(ax25_decode_ui(raw, raw_len - 2, &out) == LHKT_ERR_SHORT);
}

static void test_decode_reject_missing_final_address_bit(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    raw[13] &= (uint8_t)~0x01;

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_FORMAT);
}

static void test_decode_reject_wrong_pid(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    raw[15] = 0x00; /* Wrong PID */

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_UNSUPPORTED);
}

static void test_reject_non_ui(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);

    in.payload[0] = 'x';
    in.payload_len = 1;

    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    raw[14] = 0x13; /* Not UI */

    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_UNSUPPORTED);
}

static void test_decode_reject_bad_callsign_char(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);
    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);
    in.payload[0] = 'x';
    in.payload_len = 1;
    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    /* Inject a TNC2 delimiter (':' = 0x3A) as the first destination-callsign
     * character (AX.25 stores char << 1). Must be rejected, not passed through
     * into the formatted line. */
    raw[0] = (uint8_t)(':' << 1);
    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_FORMAT);

    /* '>' in the source callsign is likewise rejected. */
    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);
    raw[7] = (uint8_t)('>' << 1);
    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_FORMAT);
}

static void test_decode_reject_embedded_space(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    uint8_t raw[LHKT_AX25_MAX_FRAME];
    size_t raw_len = 0;

    ax25_frame_init(&in);
    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);
    in.payload[0] = 'x';
    in.payload_len = 1;
    assert(ax25_encode_ui(&in, raw, sizeof(raw), &raw_len) == LHKT_OK);

    /* Embedded space in the destination callsign ("A RS"): a space is valid
     * only as trailing padding, so the decoder must reject it. */
    raw[1] = (uint8_t)(' ' << 1);
    assert(ax25_decode_ui(raw, raw_len, &out) == LHKT_ERR_FORMAT);
}

int main(void)
{
    test_addr_parse_format();
    test_addr_accept_boundaries();
    test_addr_reject_invalid();
    test_encode_decode_roundtrip();
    test_decode_command_cbit_is_not_repeated();
    test_decode_ignores_bit7_on_both_stations();
    test_decode_path_hbit_is_repeated();
    test_encode_sets_command_cbits();
    test_decode_reject_short_frame();
    test_decode_reject_missing_final_address_bit();
    test_decode_reject_wrong_pid();
    test_reject_non_ui();
    test_decode_reject_bad_callsign_char();
    test_decode_reject_embedded_space();

    puts("test_ax25: OK");
    return 0;
}
