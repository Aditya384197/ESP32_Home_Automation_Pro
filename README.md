# ESP32 Smart Home — 24/7 Stability Revision

This revision keeps the original offline-first smart-home architecture and focuses on reliability, recovery, storage integrity, cloud resilience, safer JSON handling, and long-running operation.

**OTA is intentionally not included in this revision.** The existing factory-only partition table is therefore preserved. Do not flash an OTA partition table with this source revision.

## What was hardened

### Local control
- Relay state changes are persisted with a coalescing save task so rapid toggles do not cause a flash write for every transition.
- Remote, schedule, physical-switch and HTTP relay changes only trigger a persistence write when the logical state actually changes.
- Relay configuration/state snapshots are protected by the existing mutexes.
- JSON request parsing uses cJSON rather than fragile substring parsing.
- JSON responses containing user-configurable strings are generated through cJSON where escaping matters.
- Captive portal DNS remains local-only and does not depend on Internet access.

### Wi-Fi resilience
- AP + STA operation remains unchanged.
- STA reconnects are handled by a dedicated task with bounded backoff rather than repeatedly calling `esp_wifi_connect()` directly from the Wi-Fi event callback.
- Manual offline mode stops reconnect attempts until the user enables connectivity again.
- STA configuration can still be changed without a full restart.

### Cloud resilience
- Cloud polling is reduced to a 5-second normal interval.
- Cloud failures use bounded exponential backoff up to 60 seconds instead of hammering a failed network/service.
- Oversized/truncated HTTP responses are rejected rather than treated as successful responses.
- HTTPS is required for the cloud endpoint.
- Cloud request buffers are reused per request without the previous 16 KiB heap capture allocation.
- Command delivery now uses explicit command IDs and acknowledgements. A command is not deleted merely because the server returned it; the device acknowledges it on the next successful poll.
- Commands have a 5-minute expiry to prevent stale remote relay actions after long outages.
- Schedule replacement on the server is performed as a D1 batch operation rather than a long sequence of independent writes.
- Cached schedules remain local and continue to execute when the cloud is unavailable.

### Authentication/backend
- New passwords are stored using PBKDF2-HMAC-SHA-256 with a per-password salt and 100,000 iterations.
- Existing legacy `salt$SHA256` password records remain readable so an existing deployment is not locked out; new passwords use the stronger format.
- Login/session input validation is stricter.
- Device command authorization and device-token hashing remain in place.

## Build

The project continues to target the original ESP-IDF version configured by `.github/workflows/build.yml`.

```text
idf.py set-target esp32
idf.py build
```

The repository's CI workflow also performs the build on GitHub Actions.

## First installation

The existing `partitions.csv` is intentionally unchanged in this revision because OTA is deferred.

For a normal wired installation, flash the bootloader, partition table, and application produced by the build at the offsets reported by ESP-IDF/esptool.

## Configuration

Local AP settings, relay configuration, relay states, STA credentials, cloud credentials, brand name, and cached schedules are stored in NVS.

Cloud is optional. Local relay control and cached schedules do not require Internet access.

## Important hardware note

`RELAY_ACTIVE_LEVEL` is intentionally preserved from the supplied source. Verify the actual relay module's electrical active level before changing it; changing it blindly can invert relay behavior.

## OTA status

OTA is deliberately **out of scope for this revision**. Do not add `dual application partitions`, `dual application partitions`, or `otadata` manually to the supplied partition table. A later OTA revision should be designed as a coordinated bootloader/partition/firmware/backend feature with rollback and signed-image verification.


## Production Core v3 — hardware-independent manual control

This revision preserves the offline-first AP+STA design and adds an optional
independent relay-control backend. The AP remains enabled regardless of STA or
Internet status.

### Independent physical control

The recommended production hardware uses a small secondary MCU to own the five
physical switch inputs and five relay-driver outputs. The ESP32 then acts as the
smart/network supervisor. If the ESP32 is rebooting, disconnected, stuck in a
boot loop, or otherwise unavailable, the secondary controller continues to
process the physical switches.

The supplied ESP32 source supports both modes:

```text
RELAY_BACKEND_SECONDARY = 0
```

keeps the original direct-GPIO hardware behavior.

```text
RELAY_BACKEND_SECONDARY = 1
```

selects the independent secondary-controller backend. Do not enable that mode
until the secondary controller has been installed and its low-voltage control
connections have been verified.

The secondary-controller firmware is under:

```text
hardware/secondary_controller_attiny1616/
```

The ESP32/secondary-controller link uses framed 115200-8N1 messages with CRC16.
The secondary controller stores relay state locally, debounces physical inputs,
and reports relay state back to the ESP32.

### Diagnostics

The local API includes:

```text
GET /api/health
```

which reports uptime, current/minimum free heap, reset reason, STA/cloud state,
and relay-backend health.

mDNS is also enabled:

```text
http://smart-home.local/
```

### OTA

OTA remains deliberately out of scope for this revision. The factory-only
partition table is preserved.
