#include "tnc2.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_strip_eol(void)
{
    char line1[] = "ABC\r\n";
    char line2[] = "ABC\n";
    char line3[] = "ABC";

    assert(tnc2_strip_eol(line1) == LHKT_OK);
    assert(strcmp(line1, "ABC") == 0);

    assert(tnc2_strip_eol(line2) == LHKT_OK);
    assert(strcmp(line2, "ABC") == 0);

    assert(tnc2_strip_eol(line3) == LHKT_OK);
    assert(strcmp(line3, "ABC") == 0);
}

static void test_parse_position(void)
{
    ax25_frame_t frame;
    const char *line = "DJ0CHE-10>APLG01,WIDE1-1,WIDE2-1:!4800.00N/01100.00E-Test";

    assert(tnc2_parse_line(line, &frame) == LHKT_OK);

    assert(strcmp(frame.src.call, "DJ0CHE") == 0);
    assert(frame.src.ssid == 10);

    assert(strcmp(frame.dst.call, "APLG01") == 0);
    assert(frame.dst.ssid == 0);

    assert(frame.path_len == 2);
    assert(strcmp(frame.path[0].call, "WIDE1") == 0);
    assert(frame.path[0].ssid == 1);
    assert(strcmp(frame.path[1].call, "WIDE2") == 0);
    assert(frame.path[1].ssid == 1);

    assert(frame.payload_len == strlen("!4800.00N/01100.00E-Test"));
    assert(memcmp(frame.payload,
                  "!4800.00N/01100.00E-Test",
                  frame.payload_len) == 0);
}

static void test_parse_message_payload_unchanged(void)
{
    ax25_frame_t frame;
    const char *line = "DJ0CHE-10>APRS::DC2EH-11:Hello test{01";
    const char *payload = ":DC2EH-11:Hello test{01";

    assert(tnc2_parse_line(line, &frame) == LHKT_OK);

    assert(strcmp(frame.src.call, "DJ0CHE") == 0);
    assert(frame.src.ssid == 10);
    assert(strcmp(frame.dst.call, "APRS") == 0);
    assert(frame.path_len == 0);

    assert(frame.payload_len == strlen(payload));
    assert(memcmp(frame.payload, payload, frame.payload_len) == 0);
}

static void test_repeated_digipeater_roundtrip(void)
{
    ax25_frame_t frame;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;

    assert(tnc2_parse_line("DJ0CHE-10>APRS,WIDE1-1*:hello", &frame) == LHKT_OK);
    assert(frame.path_len == 1);
    assert(strcmp(frame.path[0].call, "WIDE1") == 0);
    assert(frame.path[0].ssid == 1);
    assert(frame.path[0].repeated == 1);

    assert(tnc2_format_line(&frame, line, sizeof(line), &line_len) == LHKT_OK);
    assert(strcmp(line, "DJ0CHE-10>APRS,WIDE1-1*:hello") == 0);
    assert(line_len == strlen(line));
}

static void test_format_roundtrip(void)
{
    ax25_frame_t in;
    ax25_frame_t out;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;
    const char *payload = ":DC2EH-11:Hello test{01";

    ax25_frame_init(&in);

    assert(ax25_addr_parse("APRS", &in.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &in.src) == LHKT_OK);
    assert(ax25_addr_parse("WIDE1-1", &in.path[0]) == LHKT_OK);
    in.path_len = 1;

    memcpy(in.payload, payload, strlen(payload));
    in.payload_len = strlen(payload);

    assert(tnc2_format_line(&in, line, sizeof(line), &line_len) == LHKT_OK);
    assert(strcmp(line, "DJ0CHE-10>APRS,WIDE1-1::DC2EH-11:Hello test{01") == 0);
    assert(line_len == strlen(line));

    assert(tnc2_parse_line(line, &out) == LHKT_OK);
    assert(strcmp(out.src.call, "DJ0CHE") == 0);
    assert(out.src.ssid == 10);
    assert(strcmp(out.dst.call, "APRS") == 0);
    assert(out.path_len == 1);
    assert(strcmp(out.path[0].call, "WIDE1") == 0);
    assert(out.path[0].ssid == 1);
    assert(out.payload_len == strlen(payload));
    assert(memcmp(out.payload, payload, out.payload_len) == 0);
}

/*
 * A frame from graywolf v0.14.12 (captured over KISS/TCP) carries the AX.25
 * command C bit on the destination. That bit is not a repeated marker, so the
 * rendered line must read "APGRWO", never "APGRWO*", or the '*' would travel
 * on to RF and APRS-IS.
 */
static void test_format_command_frame_has_no_dst_star(void)
{
    static const uint8_t raw[] = {
        0x82, 0xa0, 0x8e, 0xa4, 0xae, 0x9e, 0xe0,       /* APGRWO,  C=1 */
        0x88, 0x98, 0x62, 0xa8, 0xa6, 0xa8, 0x74,       /* DL1TST-10, C=0 */
        0xae, 0x92, 0x88, 0x8a, 0x62, 0x40, 0x63,       /* WIDE1-1, H=0, last */
        0x03, 0xf0,
        '!', '4', '8', '2', '7', '.', '7', '0', 'N'
    };
    ax25_frame_t frame;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;

    assert(ax25_decode_ui(raw, sizeof(raw), &frame) == LHKT_OK);
    assert(tnc2_format_line(&frame, line, sizeof(line), &line_len) == LHKT_OK);
    assert(strcmp(line, "DL1TST-10>APGRWO,WIDE1-1:!4827.70N") == 0);
    assert(line_len == strlen(line));
}

/* A '*' on the destination of an inbound line is tolerated but dropped. */
static void test_parse_drops_dst_star(void)
{
    ax25_frame_t frame;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;

    assert(tnc2_parse_line("DL1ABC>APRS*,WIDE1-1:!test", &frame) == LHKT_OK);
    assert(frame.dst.repeated == 0);
    assert(frame.src.repeated == 0);
    assert(frame.path_len == 1);

    assert(tnc2_format_line(&frame, line, sizeof(line), &line_len) == LHKT_OK);
    assert(strcmp(line, "DL1ABC>APRS,WIDE1-1:!test") == 0);
}

/*
 * The renderer itself must never star a dst/src, even for a frame assembled by
 * hand rather than parsed — that is what keeps a beacon or a config-supplied
 * callsign from putting "DST*" on the air. Path stars still render.
 */
static void test_format_never_stars_a_station(void)
{
    ax25_frame_t frame;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;

    ax25_frame_init(&frame);

    assert(ax25_addr_parse("APRS*", &frame.dst) == LHKT_OK);
    assert(ax25_addr_parse("DL1ABC-9*", &frame.src) == LHKT_OK);
    assert(ax25_addr_parse("WIDE1-1*", &frame.path[0]) == LHKT_OK);
    frame.path_len = 1;
    frame.payload[0] = 'x';
    frame.payload_len = 1;

    assert(frame.dst.repeated == 1 && frame.src.repeated == 1);

    assert(tnc2_format_line(&frame, line, sizeof(line), &line_len) == LHKT_OK);
    assert(strcmp(line, "DL1ABC-9>APRS,WIDE1-1*:x") == 0);
}

static void test_reject_oversized_format_path(void)
{
    ax25_frame_t frame;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;

    ax25_frame_init(&frame);

    assert(ax25_addr_parse("APRS", &frame.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &frame.src) == LHKT_OK);

    frame.path_len = LHKT_AX25_MAX_REPEATERS + 1;

    assert(tnc2_format_line(&frame, line, sizeof(line), &line_len) == LHKT_ERR_LONG);
}

static void test_reject_nul_payload(void)
{
    ax25_frame_t frame;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;

    ax25_frame_init(&frame);

    assert(ax25_addr_parse("APRS", &frame.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &frame.src) == LHKT_OK);

    frame.payload[0] = 'A';
    frame.payload[1] = 0;
    frame.payload[2] = 'B';
    frame.payload_len = 3;

    assert(tnc2_format_line(&frame, line, sizeof(line), &line_len) == LHKT_ERR_FORMAT);
}

static void test_reject_oversized_format_payload(void)
{
    ax25_frame_t frame;
    char line[LHKT_TNC2_MAX_LINE];
    size_t line_len = 0;

    ax25_frame_init(&frame);

    assert(ax25_addr_parse("APRS", &frame.dst) == LHKT_OK);
    assert(ax25_addr_parse("DJ0CHE-10", &frame.src) == LHKT_OK);

    frame.payload_len = LHKT_AX25_MAX_PAYLOAD + 1;

    assert(tnc2_format_line(&frame, line, sizeof(line), &line_len) == LHKT_ERR_LONG);
}

static void test_reject_invalid(void)
{
    ax25_frame_t frame;
    char long_payload[LHKT_TNC2_MAX_LINE];
    char too_many_path[LHKT_TNC2_MAX_LINE];
    size_t prefix_len;
    size_t i;

    assert(tnc2_parse_line("DJ0CHE-10APRS:payload", &frame) == LHKT_ERR_FORMAT);
    assert(tnc2_parse_line("DJ0CHE-10>APRS", &frame) == LHKT_ERR_FORMAT);
    assert(tnc2_parse_line(">APRS:payload", &frame) == LHKT_ERR_FORMAT);
    assert(tnc2_parse_line("DJ0CHE-10>:payload", &frame) == LHKT_ERR_FORMAT);
    assert(tnc2_parse_line("DJ0CHE-10>APRS,:payload", &frame) == LHKT_ERR_FORMAT);
    assert(tnc2_parse_line("DJ0CHE-10>APRS,,WIDE1-1:payload", &frame) == LHKT_ERR_FORMAT);

    snprintf(too_many_path,
             sizeof(too_many_path),
             "DJ0CHE-10>APRS,WIDE1-1,WIDE2-1,WIDE3-1,WIDE4-1,WIDE5-1,WIDE6-1,WIDE7-1,WIDE8-1,WIDE9-1:payload");

    assert(tnc2_parse_line(too_many_path, &frame) == LHKT_ERR_LONG);

    snprintf(long_payload,
             sizeof(long_payload),
             "DJ0CHE-10>APRS:");

    prefix_len = strlen(long_payload);
    for (i = prefix_len; i < prefix_len + LHKT_AX25_MAX_PAYLOAD + 1; i++) {
        long_payload[i] = 'A';
    }
    long_payload[prefix_len + LHKT_AX25_MAX_PAYLOAD + 1] = '\0';

    assert(tnc2_parse_line(long_payload, &frame) == LHKT_ERR_LONG);
}

int main(void)
{
    test_strip_eol();
    test_parse_position();
    test_parse_message_payload_unchanged();
    test_repeated_digipeater_roundtrip();
    test_format_roundtrip();
    test_format_command_frame_has_no_dst_star();
    test_parse_drops_dst_star();
    test_format_never_stars_a_station();
    test_reject_oversized_format_path();
    test_reject_nul_payload();
    test_reject_oversized_format_payload();
    test_reject_invalid();

    puts("test_tnc2: OK");
    return 0;
}
