# ATtiny1626 Hardware Integration

ESP32 smart domain:
- AP always ON
- STA independently reconnecting
- local web, schedules, cloud and diagnostics

Independent relay domain:
- physical switches -> ATtiny1626 -> relay drivers
- ESP32 <-> ATtiny1626 via 115200 8N1 UART
- ESP32 TX GPIO22 -> ATtiny PB3 (RX)
- ESP32 RX GPIO23 <- ATtiny PB2 (TX)

The physical-control path does not require an ESP32 heartbeat.

Verify logic levels and relay-driver polarity on low-voltage hardware before
connecting any load. Mains protection, isolation, fusing/MCB, relay ratings,
creepage/clearance and thermal design must be handled separately.
