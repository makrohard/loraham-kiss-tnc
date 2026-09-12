#ifndef LHKT_RFLOG_H
#define LHKT_RFLOG_H

/*
 * RF log: what this TNC's radio heard and sent, one persistent line per frame.
 *
 * The controller names the file (`--rflog on --rflog-path <absolute>`); this
 * process derives nothing. Line contract, shared with the other LoRaHAM writers:
 *
 *   <utc> RX rssi=<dBm> snr=<dB> len=<n> tnc2="<text>" hex=<..> ascii="<..>"
 *   <utc> TX rssi=- snr=- len=<n> outcome=<ok|unconfirmed> tnc2="<text>" hex=<..> ascii="<..>"
 *
 * RSSI/SNR are receive metadata; a TX line carries payload and outcome. The
 * TNC2 text is the summary this boundary already yields — it is recovered from
 * the packet the send/receive path owns (0x3c 0xff 0x01 + TNC2), never carried
 * separately. Absent marker: no tnc2 field, the raw payload still logs.
 *
 * Retention: copy-truncate at LHKT_RFLOG_MAX_BYTES into <path>.1; the inode
 * never changes, so an external truncate is tolerated (O_APPEND). One writer.
 */

#include <stddef.h>
#include <stdint.h>

#define LHKT_RFLOG_MAX_BYTES (5u * 1024u * 1024u)

/* Open for append. Absolute path only. LHKT_OK, or LHKT_ERR (path, open). */
int  lhkt_rflog_open(const char *path);
void lhkt_rflog_close(void);
int  lhkt_rflog_active(void);

void lhkt_rflog_rx(int16_t rssi_cdbm, int16_t snr_cdb, const uint8_t *packet, size_t len);
void lhkt_rflog_tx(const char *outcome, const uint8_t *packet, size_t len);

/* Pure helpers, exposed for the unit test. */
const char *lhkt_rflog_tnc2_summary(const uint8_t *packet, size_t len, char *out, size_t out_size);
size_t lhkt_rflog_format_rx(char *out, size_t out_size, const char *utc,
                            int16_t rssi_cdbm, int16_t snr_cdb,
                            const uint8_t *packet, size_t len);
size_t lhkt_rflog_format_tx(char *out, size_t out_size, const char *utc, const char *outcome,
                            const uint8_t *packet, size_t len);
void lhkt_rflog_set_max_bytes(size_t max_bytes);   /* test helper */

#endif
