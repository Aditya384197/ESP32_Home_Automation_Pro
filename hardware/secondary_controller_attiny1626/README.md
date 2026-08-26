# ATtiny1626 Independent Relay Controller

Production target: **ATtiny1626 + megaTinyCore**.

The ATtiny1626 is the independent manual-control domain. It owns the five
physical switches and five relay-driver outputs, so manual control does not
depend on the ESP32 application, Wi-Fi, Internet, or cloud.

Useful ATtiny1626 capabilities used or reserved by this design include
16 KB Flash, 2 KB SRAM, 256 B EEPROM, hardware watchdog, brown-out/power-on
reset support, two USARTs, and a 12-bit ADC with PGA. The second USART and
analog resources are intentionally left available for future diagnostics.

Pin allocation:
- PA1..PA5: switches 1..5
- PA6, PA7, PB4, PB5, PC0: relay outputs 1..5
- PB2: USART0 TX -> ESP32 RX
- PB3: USART0 RX <- ESP32 TX
- PA0: reserved for UPDI/RESET

The firmware uses level-following switch logic (active LOW = relay ON).
For momentary push-buttons, use a toggle-on-press policy instead.

The low-voltage controller still requires properly designed isolation,
protection and relay-driver hardware for any mains installation.
