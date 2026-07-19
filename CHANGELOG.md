# 0.5.0
- Add `--bind` source-IP allow-list (IPv4/CIDR, default 127.0.0.1). Only matching
  peers may connect; the listen address is derived from it; rejected peers are
  logged. Config key `bind`.
- Hardening: reject an empty CIDR prefix (`--bind x/` no longer means allow-all);
  validate AX.25 callsign charset on decode (no TNC2 delimiter injection);
  accept() backs off on fd exhaustion; LoRaHAM data socket set non-blocking;
  64-bit millisecond timestamps (no 32-bit wrap); SIGINT/SIGTERM via sigaction;
  bounded main-loop select timeout; production build flags (-O2, _FORTIFY_SOURCE,
  stack protector, PIE/RELRO/BIND_NOW).
- Fix: a rejected KISS/TCP peer no longer blocks the bridge; the listen socket is non-blocking and rejected peers return to the main loop
- Fix: framed DATA decoder state is owned by the data socket; KISS client connect/disconnect no longer desyncs the daemon stream, and daemon RX is decoded (and counted as drops) while no client is connected
- Fix: framed decode errors reconnect the data socket to resync the stream
- Fix: restore RX frequency when TX_RESULT confirmation times out
- Fix: honor the attached `-cFILE` config argument form
- Logging: line-buffered stdout (timely under systemd); per-packet log lines now require --verbose
- Cleanup: remove dead pre-framed RX state; monotonic stats scheduling; stricter KISS/AX.25/CONF edge-case parsing

# 0.4.0
- Add v111 TX_RESULT lifecycle with KISS client recovery.
- Rely on loraham_daemon 110+ (managed TX_RESULT confirmation needs 111+; 110 uses CONF fallback)

# 0.3.0
- Add CAD status counters to stats output and set default stats interval to 900s
- Remove deprecated bridge-side CAD configuration options
- Drop malformed KISS frames until the next frame delimiter
- Rely on daemon 110 CAD gate; bridge-side CAD wait options are no-ops
- Require loraham_daemon 110 framed RX metadata layout
- Strip daemon RX RSSI/SNR metadata before KISS output
- Add functional tests for daemon 110 framed RX packets

# 0.2.0
- Updated, after refactoring loraham_daemon to 109a. *This breaks comatibility to loraham_daemon < 109*
- Use framed DATA sockets
- TX Queue and delayed sending on TX=1 or CAD=1
- Use persistent CONF socket

# 0.1.0
- Initial Version, developed for loraham_daemon 108
- KISS/TCP server for APRS clients
- AX.25 / TNC2 / KISS frame handling
- LoRaHAM daemon socket integration
- CLI and config file support
- example configuration
- build and test helper scripts
- serial KISS forwarding  `start-serial-kiss.sh`
- README with usage notes
