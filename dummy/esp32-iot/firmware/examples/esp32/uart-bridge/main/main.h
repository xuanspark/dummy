// ============================================================================
//  main.h —— ESP32 uart-bridge 公共头
// ----------------------------------------------------------------------------
//  - 汇总 Mongoose / ESP-IDF 驱动头，统一 SPIFFS 路径与 UART 口号
//  - 对外暴露 uart_bridge_fn (HTTP/WS/TCP/MQTT 主处理) 、wifi_init、cli 接口
// ============================================================================
#pragma once

#include "mongoose.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"

#define FS_ROOT "/spiffs"            // SPIFFS 挂载点，存放 wifi.json / config.json
#define WIFI_FILE FS_ROOT "/wifi.json" // 多 WiFi 凭证文件（JSON 数组，最多 5 个）
#define UART_NO 1                      // 使用 UART1（UART0 被 USB-Serial 占用为控制台）

void uart_bridge_fn(struct mg_connection *, int, void *, void *);
int uart_read(void *buf, size_t len);
bool wifi_init(const char *ssid, const char *pass);
void wifi_get_ip(char *buf, size_t n);
void cli(uint8_t ch);
void cli_init(void);
void print_banner(void);
