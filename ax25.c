#include "ax25.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AX25_ADDR_LEN 7
#define AX25_CTRL_UI  0x03
#define AX25_PID_NONE 0xf0

/*
 * This module handles only AX.25 UI frames for APRS.
 * It does not use Linux kernel AX.25 and does not implement connected mode.
 *
 * SSID byte layout (AX.25 2.2, section 3.12):
 *
 *   bit 7    dst/src: command/response (C) bit
 *            path:    has-been-repeated (H) bit
 *   bit 6-5  reserved, transmitted as 1
 *   bit 4-1  SSID
 *   bit 0    address extension, set on the last address
 *
 * Bit 7 therefore means two different things depending on where the address
 * sits. APRS sends UI frames as commands, so a conforming sender sets C=1 on
 * the destination and C=0 on the source (Dire Wolf, aprx and graywolf all do).
 * Only path addresses carry the '*' repeated marker; rendering a set C bit as
 * '*' would put a bogus "DST*" into the TNC2 line and onto the air.
 */
#define AX25_ADDR_BIT7      0x80
#define AX25_ADDR_RESERVED  0x60
#define AX25_ADDR_LAST      0x01
#define AX25_ADDR_SSID_MASK 0x0f

void ax25_frame_init(ax25_frame_t *frame)
{
    if (!frame) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
}

/*
 * Parse CALL or CALL-SSID.
 * A trailing '*' marks a repeated digipeater address.
 */
int ax25_addr_parse(const char *text, ax25_addr_t *addr)
{
    char tmp[16];
    char *dash;
    size_t len;
    size_t i;
    long ssid;

    if (!text || !addr || text[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    memset(addr, 0, sizeof(*addr));

    if (strlen(text) >= sizeof(tmp)) {
        return LHKT_ERR_LONG;
    }

    snprintf(tmp, sizeof(tmp), "%s", text);

    len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '*') {
        addr->repeated = 1;
        tmp[len - 1] = '\0';
    }

    dash = strchr(tmp, '-');
    if (dash) {
        *dash = '\0';
        dash++;

        if (*dash == '\0') {
            return LHKT_ERR_FORMAT;
        }

        for (i = 0; dash[i] != '\0'; i++) {
            if (!isdigit((unsigned char)dash[i])) {
                return LHKT_ERR_FORMAT;
            }
        }

        ssid = strtol(dash, NULL, 10);
        if (ssid < 0 || ssid > 15) {
            return LHKT_ERR_FORMAT;
        }

        addr->ssid = (uint8_t)ssid;
    }

    len = strlen(tmp);
    if (len < 1 || len > 6) {
        return LHKT_ERR_FORMAT;
    }

    for (i = 0; i < len; i++) {
        if (!isalnum((unsigned char)tmp[i])) {
            return LHKT_ERR_FORMAT;
        }

        addr->call[i] = (char)toupper((unsigned char)tmp[i]);
    }

    addr->call[len] = '\0';

    return LHKT_OK;
}

/*
 * Format address as CALL or CALL-SSID.
 * Repeated path entries get a trailing '*'.
 */
int ax25_addr_format(const ax25_addr_t *addr, char *out, size_t out_size)
{
    int ret;

    if (!addr || !out || out_size == 0 || addr->call[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    if (addr->ssid > 15) {
        return LHKT_ERR_FORMAT;
    }

    if (addr->ssid == 0) {
        ret = snprintf(out, out_size, "%s%s",
                       addr->call,
                       addr->repeated ? "*" : "");
    } else {
        ret = snprintf(out, out_size, "%s-%u%s",
                       addr->call,
                       addr->ssid,
                       addr->repeated ? "*" : "");
    }

    if (ret < 0 || (size_t)ret >= out_size) {
        return LHKT_ERR_NOSPACE;
    }

    return LHKT_OK;
}

/* bit7: C bit for dst/src, H bit for path addresses. */
static int ax25_encode_addr(const ax25_addr_t *addr,
                            int last,
                            int bit7,
                            uint8_t *out)
{
    size_t i;
    char c;

    if (!addr || !out || addr->call[0] == '\0' || addr->ssid > 15) {
        return LHKT_ERR_FORMAT;
    }

    for (i = 0; i < 6; i++) {
        c = addr->call[i] ? addr->call[i] : ' ';
        out[i] = (uint8_t)((uint8_t)c << 1);
    }

    out[6] = AX25_ADDR_RESERVED |
             (uint8_t)((addr->ssid & AX25_ADDR_SSID_MASK) << 1);

    if (bit7) {
        out[6] |= AX25_ADDR_BIT7;
    }

    if (last) {
        out[6] |= AX25_ADDR_LAST;
    }

    return LHKT_OK;
}

/* is_path selects the bit 7 meaning: H bit on path addresses, C bit on
 * dst/src. A C bit is a sender/receiver role marker, never a '*'. */
static int ax25_decode_addr(const uint8_t *data, int is_path, ax25_addr_t *addr)
{
    size_t i;
    size_t len;
    char c;

    if (!data || !addr) {
        return LHKT_ERR;
    }

    memset(addr, 0, sizeof(*addr));

    for (i = 0; i < 6; i++) {
        c = (char)((data[i] >> 1) & 0x7f);
        /* Callsign characters are alphanumeric; space is trailing padding.
         * Reject anything else so a crafted AX.25 header cannot inject TNC2
         * delimiters ('>', ':', ',') into the formatted line. */
        if (c != ' ' && !isalnum((unsigned char)c)) {
            return LHKT_ERR_FORMAT;
        }
        addr->call[i] = c;
    }

    addr->call[6] = '\0';

    len = strlen(addr->call);
    while (len > 0 && addr->call[len - 1] == ' ') {
        addr->call[len - 1] = '\0';
        len--;
    }

    if (len < 1) {
        return LHKT_ERR_FORMAT;
    }

    /* A space is valid only as trailing padding (stripped above); an embedded
     * space is a malformed callsign. */
    if (strchr(addr->call, ' ')) {
        return LHKT_ERR_FORMAT;
    }

    addr->ssid = (data[6] >> 1) & AX25_ADDR_SSID_MASK;
    addr->repeated = (is_path && (data[6] & AX25_ADDR_BIT7)) ? 1 : 0;

    return LHKT_OK;
}

/*
 * Encode destination, source, optional path, then UI control/PID/payload.
 */
int ax25_encode_ui(const ax25_frame_t *frame,
                   uint8_t *out,
                   size_t out_size,
                   size_t *out_len)
{
    size_t addr_count;
    size_t need;
    size_t pos;
    size_t i;
    int ret;

    if (!frame || !out || !out_len) {
        return LHKT_ERR;
    }

    *out_len = 0;

    if (frame->path_len > LHKT_AX25_MAX_REPEATERS ||
        frame->payload_len > LHKT_AX25_MAX_PAYLOAD) {
        return LHKT_ERR_LONG;
    }

    addr_count = 2 + frame->path_len;
    need = addr_count * AX25_ADDR_LEN + 2 + frame->payload_len;

    if (need > LHKT_AX25_MAX_FRAME) {
        return LHKT_ERR_LONG;
    }

    if (out_size < need) {
        return LHKT_ERR_NOSPACE;
    }

    pos = 0;

    /* UI frames are commands: C=1 on the destination, C=0 on the source. The
     * dst/src repeated flags are deliberately ignored; '*' is path-only.
     *
     * Commands only, by scope: this bridge carries APRS, where every frame is a
     * UI command. A UI *response* (dst C=0 / src C=1) and AX.25 2.0 framing (both
     * C bits 0) are therefore not representable — the frame model has no field for
     * it, and inventing one would let a caller emit non-APRS framing onto the air.
     *
     * The destination is never the final address (a frame always has a source), so
     * its extension bit is unconditionally 0.
     */
    ret = ax25_encode_addr(&frame->dst, 0, 1, out + pos);
    if (ret != LHKT_OK) {
        return ret;
    }
    pos += AX25_ADDR_LEN;

    ret = ax25_encode_addr(&frame->src, (frame->path_len == 0), 0, out + pos);
    if (ret != LHKT_OK) {
        return ret;
    }
    pos += AX25_ADDR_LEN;

    for (i = 0; i < frame->path_len; i++) {
        ret = ax25_encode_addr(&frame->path[i],
                               (i + 1 == frame->path_len),
                               frame->path[i].repeated,
                               out + pos);
        if (ret != LHKT_OK) {
            return ret;
        }
        pos += AX25_ADDR_LEN;
    }

    out[pos++] = AX25_CTRL_UI;
    out[pos++] = AX25_PID_NONE;

    if (frame->payload_len > 0) {
        memcpy(out + pos, frame->payload, frame->payload_len);
        pos += frame->payload_len;
    }

    *out_len = pos;

    return LHKT_OK;
}

/*
 * Decode only AX.25 UI frames with PID 0xf0.
 */
int ax25_decode_ui(const uint8_t *data,
                   size_t data_len,
                   ax25_frame_t *frame)
{
    ax25_addr_t addrs[LHKT_AX25_MAX_ADDRS];
    size_t pos;
    size_t addr_count;
    size_t payload_len;
    int last;
    int ret;

    if (!data || !frame) {
        return LHKT_ERR;
    }

    ax25_frame_init(frame);

    pos = 0;
    addr_count = 0;
    last = 0;

    while (pos + AX25_ADDR_LEN <= data_len &&
           addr_count < LHKT_AX25_MAX_ADDRS) {
        ret = ax25_decode_addr(data + pos,
                               (addr_count >= 2),
                               &addrs[addr_count]);
        if (ret != LHKT_OK) {
            return ret;
        }

        last = data[pos + 6] & AX25_ADDR_LAST;
        pos += AX25_ADDR_LEN;
        addr_count++;

        if (last) {
            break;
        }
    }

    if (!last || addr_count < 2) {
        return LHKT_ERR_FORMAT;
    }

    if (pos + 2 > data_len) {
        return LHKT_ERR_SHORT;
    }

    if (data[pos] != AX25_CTRL_UI || data[pos + 1] != AX25_PID_NONE) {
        return LHKT_ERR_UNSUPPORTED;
    }

    pos += 2;
    payload_len = data_len - pos;

    if (payload_len > LHKT_AX25_MAX_PAYLOAD) {
        return LHKT_ERR_LONG;
    }

    frame->dst = addrs[0];
    frame->src = addrs[1];

    if (addr_count > 2) {
        frame->path_len = addr_count - 2;
        memcpy(frame->path, &addrs[2], frame->path_len * sizeof(ax25_addr_t));
    }

    if (payload_len > 0) {
        memcpy(frame->payload, data + pos, payload_len);
    }

    frame->payload_len = payload_len;

    return LHKT_OK;
}
