# Production Feature Set v4

## Active high-value features

- AP + STA simultaneously; AP is always available.
- STA reconnect with bounded backoff.
- Offline local DNS/captive portal.
- mDNS (`smart-home.local`).
- Local schedules continue without cloud.
- SNTP/timekeeping for schedule operation.
- NVS persistence and coalesced state writes.
- cJSON request/response handling.
- Cloud backoff, command acknowledgements and stale-command protection.
- Health endpoint with uptime, heap, reset reason and connectivity state.
- Independent ATtiny1626 relay/switch domain.
- CRC-protected ESP32 <-> ATtiny frames.
- Serialized ESP32 UART transmission.
- Secondary-controller link timeout/health monitoring.
- ATtiny watchdog and EEPROM two-slot CRC/sequence recovery.

## Deferred deliberately

OTA/dual partitions, Secure Boot/Flash Encryption, MQTT/TLS/Home Assistant,
ESP-NOW, BLE provisioning, sensor drivers and TinyML are not mixed into this
revision's relay safety path. They can be added as separately tested modules.
The factory-only partition table remains unchanged.
