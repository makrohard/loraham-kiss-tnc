# LoRaHAM KISS TNC Bridge

KISS/TCP TNC bridge for `LoRaHAM_Daemon`.

It exposes a KISS/TCP port for APRS clients and talks to the LoRaHAM daemon through its framed DATA Unix socket. The daemon socket defaults to `/run/loraham/lora433f.sock` when that socket exists (systemd deployment) and falls back to `/tmp/lora433f.sock` otherwise; override with `--data-socket`/`--conf-socket` or the config file.

```text
APRS client <-> KISS/TCP <-> loraham_kiss_tnc <-> framed DATA socket <-> loraham_daemon
```

## Compatibility

- requires `loraham_daemon` 110+; managed `TX_RESULT` confirmation requires 111+ (110 uses CONF fallback)
- uses framed DATA sockets for daemon packet I/O
- daemon 110 `RX_PACKET` RSSI/SNR metadata is stripped before KISS output
- `TX_PACKET` payloads are still sent as RF bytes without metadata
- KISS remains fire-and-forget; TX result frames are never forwarded

## Limitations
- single KISS/TCP client
- KISS port 0 only
- APRS/TNC2 text payload only
- TX packets are queued and sent one at a time
- mode=LORA only - because FSK is not used in LoRa APRS
- framed DATA socket only for daemon packet I/O
- requires `loraham_daemon` 110+ framed RX metadata layout
- RX RSSI/SNR metadata is ignored and not forwarded to KISS clients

## TX policy

- queued TX waits for fresh `STATUS` after CONF reconnect
- `TX=1` delays TX until `TX=0`; timeout drops the packet
- daemon 110 performs the final CAD gate before RF transmit
- CAD daemon status is retained for stats output
- bridge-side CAD wait/ignore options were removed; daemon 110 owns channel gating

The bridge intentionally does not requeue daemon `CAD_TIMEOUT` framed errors. The framed DATA protocol has no TX/error correlation, so requeueing could duplicate or misassign APRS packets. There are no bridge-side `--cad-*` policy options.

## Build

```bash
./loraham_kiss_tnc/build.sh
```

## Test

```bash
./loraham_kiss_tnc/run_tests.sh
```

## Run

```bash
./loraham_kiss_tnc/loraham_kiss_tnc --help
```

## Usage

```text
Usage: ./loraham-kiss-tnc [OPTIONS]

LoRaHAM KISS/TCP TNC bridge

Options:
  -c, --config FILE        Load config file
      --bind CIDR          Source allow-list: IPv4 or CIDR that may
                           connect. 127.0.0.1 (default, loopback),
                           192.168.0.0/24 (a LAN), 0.0.0.0/0 (any).
                           KISS has no auth. Derives the listen address.
      --kiss-host HOST     Explicit listen address (advanced; overrides
                           the address derived from --bind)
      --kiss-port PORT     KISS/TCP listen port
      --data-socket PATH   LoRaHAM framed data socket
      --conf-socket PATH   LoRaHAM config socket
      --rx-freq MHz        RX frequency
      --tx-freq MHz        TX frequency
      --rx-only            Disable TX
      --tx-settle-ms MS    Wait after TX freq switch
      --tx-return-ms MS    Fallback wait after TX
      --tx-busy-timeout-ms MS
                            Max wait for local TX busy
      --tx-queue-len N     Queued TX packet limit
      --tx-packet-ttl-ms MS
                            Max queued packet lifetime
  -v, --verbose            Verbose output
      --version            Print version and exit
  -h, --help               Show help
```

## Network exposure (`--bind`)

`--bind` is a source-IP allow-list: only peers whose address falls inside the
given IPv4/CIDR may connect. It also derives the listen address — loopback when
the allowed network is within `127.0.0.0/8` (so the port stays unexposed),
otherwise all interfaces (`0.0.0.0`) with the source filter applied on
`accept()`.

| `--bind` | Who may connect | Listens on |
|----------|-----------------|-----------|
| `127.0.0.1` (default) | this host only | `127.0.0.1` |
| `192.168.178.0/24`    | that LAN subnet | `0.0.0.0` |
| `10.0.0.5`            | one host (/32)  | `0.0.0.0` |
| `0.0.0.0/0`           | anyone          | `0.0.0.0` |

A plain address is treated as `/32`. Config-file key: `bind = <ipv4|cidr>`.
`--kiss-host` sets the listen address explicitly (advanced) while the allow-list
still comes from `--bind`.

**The KISS protocol has no authentication and this bridge adds none.** Anyone
allowed to connect can transmit on your station and read received traffic.
Keep the allow-list as narrow as possible; for access beyond a trusted LAN,
prefer a VPN/SSH tunnel and leave `--bind 127.0.0.1`. Rejected peers are logged
(`[KISS] rejected connection from <ip> (not in allow-list)`).

## Serial KISS

Xastir only supports Serial KISS TNC. You can use socat to forward the byte-stream to a PTY.
This works over network, too.

Start socat on the machine with the APRS-Client and point to the machine running the TNC.

```bash
socat -d -d \
  PTY,link=/tmp/loraham_kiss,raw,echo=0,waitslave \
  TCP:127.0.0.1:8001
```


To reach the TNC from another machine, widen the allow-list with `--bind`
(e.g. `--bind 192.168.0.0/24` for a LAN); the default `--bind 127.0.0.1`
listens on loopback only. See "Network exposure" above.
```bash
./loraham_kiss_tnc/loraham_kiss_tnc \
  --bind 192.168.0.0/24 \
  --kiss-port 8001
```
The included shell script ```start-serial-kiss.sh``` starts socat configured for local use.

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Johannes Loose <410733@gmail.com>.
