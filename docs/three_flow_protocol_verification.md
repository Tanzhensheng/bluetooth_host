# Three Flow Protocol Verification

This note covers the current two-machine BLE protocol demo:

- host: `project002_ble_host_demo`
- sensor terminal: `project003_ble_sensor_terminal`
- transport boundary: do not modify `ble_link.c` or the sensor BLE/GATT transport

## Common Setup

On the sensor terminal Linux host:

```bash
cd ~/Documents/bluetooth_sensor
cmake --build build
./build/ble_sensor_terminal
```

On the host Linux machine:

```bash
cd ~/bluetooth/bluetooth_host
git pull
cmake --build build
```

Use the same GATT UUIDs already verified:

```bash
TARGET=50:84:92:3F:F2:2C
SERVICE=9b0a0001-5f3b-4b5c-8c71-000000000001
WRITE=9b0a0002-5f3b-4b5c-8c71-000000000001
NOTIFY=9b0a0003-5f3b-4b5c-8c71-000000000001
```

## Normal Flow

Command:

```bash
./build/mod_ble_demo \
  --target "$TARGET" \
  --service-uuid "$SERVICE" \
  --write-char-uuid "$WRITE" \
  --notify-char-uuid "$NOTIFY" \
  --flow normal \
  01 02 03
```

Expected host log markers:

```text
flow send request
flow terminal ack
flow rx ... fseq=0x82
host ack ... nack=0
flow rx ... fseq=0x01
host ack ... nack=0
flow rx ... fseq=0x02
host ack ... nack=0
demo completed with ... bytes of payload
[PAYLOAD] ...
```

Expected sensor log markers:

```text
prepared 3 business frames
TX data seq=0 ... reason=next
host acked tx ...
TX data seq=1 ... reason=next
host acked tx ...
TX data seq=2 ... reason=next
business reply complete
```

## Abnormal Retry Flow

Command:

```bash
./build/mod_ble_demo \
  --target "$TARGET" \
  --service-uuid "$SERVICE" \
  --write-char-uuid "$WRITE" \
  --notify-char-uuid "$NOTIFY" \
  --flow abnormal \
  01 02 03
```

Expected behavior:

- host sends one NACK for `seq=1`
- sensor resends that same logical frame with a new `PSEQ`
- host ACKs the resent frame
- transaction still completes

Expected host log markers:

```text
flow simulate abnormal nack seq=1
host ack ... nack=1
flow rx ... fseq=0x01
host ack ... nack=0
demo completed with ... bytes of payload
```

Expected sensor log markers:

```text
host nacked tx ...
TX data seq=1 ... reason=nack
host acked tx ...
business reply complete
```

## Repair Flow

Command:

```bash
./build/mod_ble_demo \
  --target "$TARGET" \
  --service-uuid "$SERVICE" \
  --write-char-uuid "$WRITE" \
  --notify-char-uuid "$NOTIFY" \
  --flow repair \
  01 02 03
```

Expected behavior:

- host repeatedly NACKs `seq=1`
- sensor retries until `resend_limit` is exhausted
- sensor advances to later frames
- host times out with `seq=1` missing and sends `FUN=02` repair request
- sensor ACKs repair request and resends `seq=1`
- host ACKs repaired frame and completes reassembly

Expected host log markers:

```text
flow simulate missing seq=1 with nack
host repair request missing_seq=1
flow terminal ack
flow rx ... fseq=0x01
host ack ... nack=0
demo completed with ... bytes of payload
```

Expected sensor log markers:

```text
TX seq=1 retry exhausted, advance and wait for repair
repair request seq=1
TX data seq=1 ... reason=next
repair frame acked
```

## Notes

- `--flow normal` is the default.
- `--flow abnormal` uses NACK to exercise the abnormal retry path.
- `--flow repair` simulates a missing logical `SEQ=1` frame and then requests repair.
- `control=0x43` is still available in old single-frame helpers, but the flow demo uses `FUN=01` business requests.
