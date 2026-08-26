# ESP32 Smart Home — Production Architecture

## Design goal

The system is split into two reliability domains:

1. **ESP32 smart domain** — Wi-Fi AP+STA, local web UI, cloud, scheduling,
   diagnostics, future MQTT/ESP-NOW/BLE/sensor services.
2. **Independent relay domain** — physical switch handling and relay-driver
   control on a small secondary MCU.

The second domain is the important hardware-fail-safe improvement: the ESP32 is
not the only path between a physical wall switch and a relay output.

## AP + STA policy

The ESP32 always starts in `WIFI_MODE_APSTA`.

- AP is local and remains available even if STA cannot connect.
- STA reconnects with bounded backoff.
- User offline mode pauses STA reconnects.
- Cloud availability is independent from local relay operation.
- Cached schedules remain usable without cloud access.

## Relay authority

### With the original direct-GPIO backend

```text
Physical switch -> ESP32 -> relay driver
Web/cloud/schedule -> ESP32 -> relay driver
```

This mode is retained for compatibility.

### With the production secondary backend

```text
                         +--------------------+
                         |       ESP32        |
                         | Wi-Fi/Web/Cloud    |
                         | Scheduler/Health   |
                         +---------+----------+
                                   |
                              CRC16 UART
                                   |
                         +---------v----------+
                         |  Secondary MCU     |
                         | Switch + Relay     |
                         | Watchdog + EEPROM  |
                         +----+----------+----+
                              |          |
                         switches    relay drivers
```

The secondary controller remains usable when the ESP32 is unavailable.

## State synchronization

On secondary-controller boot, its locally stored relay state is authoritative.
The ESP32 requests a state report rather than immediately overwriting the
secondary state with its older NVS snapshot.

When a physical switch changes, the secondary sends a `CMD_SWITCH` report. The
ESP32 updates its in-memory/NVS state and marks the change as a manual event.

When the ESP32 issues a smart command, it sends a CRC-protected `CMD_SET` frame.
The secondary applies the state and reports the resulting state.

## Failure behavior

| Failure | Local AP | Physical switches | Smart control |
|---|---|---|---|
| Internet unavailable | Yes | Yes | Local only |
| STA unavailable | Yes | Yes | Local only |
| Cloud unavailable | Yes | Yes | Local web/schedules |
| ESP32 application restart | Yes | Yes with secondary | Temporary interruption |
| ESP32 boot loop | Yes while powered | Yes with secondary | Unavailable |
| ESP32 completely dead | No | Yes with independent power/controller | Unavailable |
| Secondary MCU failure | Yes | Hardware-dependent | Hardware-dependent |
| Main power failure | No | No | No |

## Watchdogs

The ESP32 uses the ESP-IDF Task Watchdog for long-running tasks.
The secondary controller enables its MCU watchdog and does not depend on ESP32
heartbeat for manual operation.

The secondary controller also coalesces EEPROM writes so switch bounce and rapid
command changes do not create unnecessary EEPROM wear.

## Hardware safety boundary

Software is not the primary protection against hazardous mains faults. A real
installation still requires correctly rated isolation, fusing/MCB or equivalent
protection, relay/contactor ratings, PCB creepage/clearance, thermal design and
an appropriate enclosure. These must be designed and installed by a qualified
person.

## Current production scope

Included now:

- offline-first AP+STA
- bounded STA reconnect
- local DNS/captive portal
- local web control
- NVS persistence
- cached schedules
- cloud backoff and command ACKs
- duplicate command suppression while ACK is pending
- cJSON request/response handling
- mDNS
- health diagnostics
- optional independent relay controller
- secondary-controller watchdog/state persistence

Deferred deliberately:

- OTA/dual application partitions
- signed firmware and rollback
- MQTT/TLS/Home Assistant discovery
- ESP-NOW
- BLE provisioning
- sensor drivers and TinyML
- Secure Boot/Flash Encryption production provisioning

Those should be added as separate, tested modules rather than mixed into the
relay safety path.

## v4
The independent relay controller target is ATtiny1626. The ESP32 remains a smart supervisor; the secondary MCU remains the manual-control authority. AP and STA operate simultaneously, with AP always available.
