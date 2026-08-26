#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

/*
 * 0 = existing direct-GPIO backend (backward compatible)
 * 1 = independent low-voltage relay controller backend.
 *
 * When enabled, the physical switches and relay driver inputs must be wired to
 * the secondary controller, not to the ESP32 GPIOs. The ESP32 then becomes the
 * smart/network supervisor while the secondary controller remains the manual
 * control authority.
 */
#ifndef RELAY_BACKEND_SECONDARY
#define RELAY_BACKEND_SECONDARY 1
#endif

#define RELAY_BACKEND_COUNT 5
#define RELAY_BACKEND_UART_TX GPIO_NUM_22
#define RELAY_BACKEND_UART_RX GPIO_NUM_23
#define RELAY_BACKEND_UART_BAUD 115200
#define RELAY_BACKEND_HEALTH_TIMEOUT_MS 8000

typedef void (*relay_backend_state_cb_t)(int relay_index, bool state, bool physical_event, void *ctx);

typedef struct {
    bool enabled[RELAY_BACKEND_COUNT];
    bool state[RELAY_BACKEND_COUNT];
} relay_backend_snapshot_t;

esp_err_t relay_backend_init(const relay_backend_snapshot_t *initial);
void relay_backend_set_state_callback(relay_backend_state_cb_t cb, void *ctx);
esp_err_t relay_backend_set_state(int index, bool state);
esp_err_t relay_backend_set_enabled(int index, bool enabled, bool safe_state);
esp_err_t relay_backend_get_snapshot(relay_backend_snapshot_t *out);
bool relay_backend_is_healthy(void);
bool relay_backend_is_secondary(void);
