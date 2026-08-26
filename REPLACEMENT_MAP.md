# Replacement Map — Production Core v3

## Replace

```text
main/main.c
main/cloud_client.c
main/cloud_client.h
main/CMakeLists.txt
.github/workflows/build.yml
README.md
```

## Add

```text
main/relay_backend.c
main/relay_backend.h
PRODUCTION_ARCHITECTURE.md
REPLACEMENT_MAP.md
hardware/secondary_controller_attiny1626/relay_controller_attiny1626.ino
hardware/secondary_controller_attiny1626/README.md
```

## Keep unchanged

```text
partitions.csv
sdkconfig.defaults
CMakeLists.txt
server/schema.sql
server/src/index.js
server/public/index.html
server/migrations/001_schedule_duration.sql
server/migrations/002_reliable_commands.sql
```

The cloud/server files are intentionally not replaced in this v3 hardware
revision because the focus is the relay safety boundary and ESP32 stability.

## Backend selection

The default remains backward-compatible direct GPIO mode:

```c
#define RELAY_BACKEND_SECONDARY 0
```

After the secondary low-voltage controller is installed and verified, change the
single definition in `main/relay_backend.h` to `1` and rebuild.

ESP32 UART in secondary mode:

- GPIO22 = TX
- GPIO23 = RX
- 115200 baud, 8N1

Only use these as low-voltage logic connections.


## v4 hardware replacement
Remove `hardware/secondary_controller_attiny1616/` and use `hardware/secondary_controller_attiny1626/`. The production backend default is now `RELAY_BACKEND_SECONDARY=1`.
