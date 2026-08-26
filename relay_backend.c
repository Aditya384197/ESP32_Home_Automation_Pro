#include "relay_backend.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

#define TAG "RELAY_BACKEND"
#define UART_PORT UART_NUM_1
#define DIRECT_RELAY_ACTIVE_LEVEL 1
#define SOF 0xA5
#define CMD_SET 0x10
#define CMD_GET 0x11
#define CMD_PING 0x12
#define CMD_STATE 0x20
#define CMD_SWITCH 0x21
#define CMD_HELLO 0x30
#define FRAME_LEN 7

static relay_backend_snapshot_t g_snapshot;
static SemaphoreHandle_t g_mutex;
static relay_backend_state_cb_t g_cb;
static void *g_cb_ctx;
static TaskHandle_t g_rx_task;
static SemaphoreHandle_t g_tx_mutex;
static volatile bool g_healthy = false;
static volatile TickType_t g_last_valid_rx = 0;
#ifndef RELAY_BACKEND_SECONDARY
#define RELAY_BACKEND_SECONDARY 0
#endif
static uint8_t g_seq = 1;

static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

#if !RELAY_BACKEND_SECONDARY
static const gpio_num_t direct_relay_pins[RELAY_BACKEND_COUNT] = {GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21};
#endif

static void frame_send(uint8_t cmd, uint8_t index, uint8_t value)
{
    uint8_t f[FRAME_LEN];
    if (g_tx_mutex) xSemaphoreTake(g_tx_mutex, portMAX_DELAY);
    f[0] = SOF; f[1] = cmd; f[2] = index; f[3] = value;
    f[4] = g_seq++; f[5] = 0; f[6] = 0;
    uint16_t c = crc16(&f[1], 4);
    f[5] = (uint8_t)(c & 0xFF);
    f[6] = (uint8_t)(c >> 8);
    uart_write_bytes(UART_PORT, (const char *)f, sizeof(f));
    if (g_tx_mutex) xSemaphoreGive(g_tx_mutex);
}

#if RELAY_BACKEND_SECONDARY
static void apply_report(uint8_t cmd, uint8_t index, uint8_t value)
{
    if (index >= RELAY_BACKEND_COUNT) return;
    bool changed = false;
    bool state = value != 0;
    if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_snapshot.state[index] != state) {
        g_snapshot.state[index] = state;
        changed = true;
    }
    relay_backend_state_cb_t cb = g_cb;
    void *ctx = g_cb_ctx;
    if (g_mutex) xSemaphoreGive(g_mutex);
    if (changed && cb) cb(index, state, cmd == CMD_SWITCH, ctx);
    g_healthy = true;
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t frame[FRAME_LEN];
    size_t pos = 0;
    TickType_t last_rx = xTaskGetTickCount();
    for (;;) {
        uint8_t b;
        int n = uart_read_bytes(UART_PORT, &b, 1, pdMS_TO_TICKS(250));
        if (n <= 0) {
            if ((xTaskGetTickCount() - last_rx) > pdMS_TO_TICKS(10000)) g_healthy = false;
            continue;
        }
        last_rx = xTaskGetTickCount();
        g_last_valid_rx = last_rx;
        g_healthy = true;
        if (pos == 0 && b != SOF) continue;
        frame[pos++] = b;
        if (pos < FRAME_LEN) continue;
        pos = 0;
        uint16_t got = (uint16_t)frame[5] | ((uint16_t)frame[6] << 8);
        if (crc16(&frame[1], 4) != got) continue;
        switch (frame[1]) {
            case CMD_STATE:
            case CMD_SWITCH:
                apply_report(frame[1], frame[2], frame[3]);
                break;
            case CMD_HELLO:
                g_healthy = true;
                break;
            default:
                break;
        }
    }
}
#endif

esp_err_t relay_backend_init(const relay_backend_snapshot_t *initial)
{
    g_mutex = xSemaphoreCreateMutex();
    g_tx_mutex = xSemaphoreCreateMutex();
    if (!g_mutex || !g_tx_mutex) return ESP_ERR_NO_MEM;
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    if (initial) memcpy(&g_snapshot, initial, sizeof(g_snapshot));
#if RELAY_BACKEND_SECONDARY
    uart_config_t cfg = {
        .baud_rate = RELAY_BACKEND_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, RELAY_BACKEND_UART_TX, RELAY_BACKEND_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    if (xTaskCreate(rx_task, "relay_backend", 3072, NULL, 5, &g_rx_task) != pdPASS) return ESP_ERR_NO_MEM;
    vTaskDelay(pdMS_TO_TICKS(50));
    frame_send(CMD_HELLO, 0, 0);
    frame_send(CMD_GET, 0, 0);
    g_healthy = true;
#else
    uint64_t mask = 0;
    for (int i = 0; i < RELAY_BACKEND_COUNT; ++i) mask |= (1ULL << direct_relay_pins[i]);
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    for (int i = 0; i < RELAY_BACKEND_COUNT; ++i)
        gpio_set_level(direct_relay_pins[i], g_snapshot.state[i] ? DIRECT_RELAY_ACTIVE_LEVEL : !DIRECT_RELAY_ACTIVE_LEVEL);
    g_healthy = true;
#endif
    return ESP_OK;
}

void relay_backend_set_state_callback(relay_backend_state_cb_t cb, void *ctx)
{
    if (!g_mutex) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_cb = cb;
    g_cb_ctx = ctx;
    xSemaphoreGive(g_mutex);
}

esp_err_t relay_backend_set_state(int index, bool state)
{
    if (index < 0 || index >= RELAY_BACKEND_COUNT) return ESP_ERR_INVALID_ARG;
    if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_snapshot.state[index] = state;
    xSemaphoreGive(g_mutex);
#if RELAY_BACKEND_SECONDARY
    frame_send(CMD_SET, (uint8_t)index, state ? 1 : 0);
#else
    gpio_set_level(direct_relay_pins[index], state ? DIRECT_RELAY_ACTIVE_LEVEL : !DIRECT_RELAY_ACTIVE_LEVEL);
#endif
    return ESP_OK;
}

esp_err_t relay_backend_set_enabled(int index, bool enabled, bool safe_state)
{
    if (index < 0 || index >= RELAY_BACKEND_COUNT) return ESP_ERR_INVALID_ARG;
    if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_snapshot.enabled[index] = enabled;
    if (!enabled) g_snapshot.state[index] = safe_state;
    bool state = g_snapshot.state[index];
    xSemaphoreGive(g_mutex);
#if RELAY_BACKEND_SECONDARY
    frame_send(CMD_SET, (uint8_t)index, state ? 1 : 0);
#else
    gpio_set_level(direct_relay_pins[index], state ? DIRECT_RELAY_ACTIVE_LEVEL : !DIRECT_RELAY_ACTIVE_LEVEL);
#endif
    return ESP_OK;
}

esp_err_t relay_backend_get_snapshot(relay_backend_snapshot_t *out)
{
    if (!out || !g_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    memcpy(out, &g_snapshot, sizeof(*out));
    xSemaphoreGive(g_mutex);
    return ESP_OK;
}

bool relay_backend_is_healthy(void)
{
#if RELAY_BACKEND_SECONDARY
    if (!g_healthy || g_last_valid_rx == 0) return false;
    if ((xTaskGetTickCount() - g_last_valid_rx) >
        pdMS_TO_TICKS(RELAY_BACKEND_HEALTH_TIMEOUT_MS)) {
        g_healthy = false;
    }
#endif
    return g_healthy;
}
bool relay_backend_is_secondary(void) { return RELAY_BACKEND_SECONDARY != 0; }
