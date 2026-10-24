// ============================================================================
//  wifi.c —— ESP32 STA 模式 WiFi 联网（改造自 ESP-IDF wifi_station 示例）
// ----------------------------------------------------------------------------
//  - 单次 wifi_init 调用内部完成 NVS / netif / event-loop 初始化
//  - 重试策略：WIFI_MAX_RETRY 次，每次间隔 1 s；全失败后设置 WIFI_FAIL_BIT
//  - 连接超时：30 s；返回后释放 event group（事件处理器保留以供后续事件）
//  - 保存最后一次 GOT_IP 事件中的 IP，供 wifi_get_ip / banner 读取
// ============================================================================
// Code taken from the ESP32 IDF WiFi station Example

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mongoose.h"

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAX_RETRY 5

static int s_retry_num = 0;
static bool s_wifi_inited = false;  // 仅需一次初始化的资源（netif/event-loop）是否已完成
static uint32_t s_ip_addr = 0;       // 最后一次获得的 IPv4（网络字节序）

// event_handler：ESP-IDF WiFi / IP 事件回调。
//   - STA_START 后主动发起连接
//   - DISCONNECTED 进入重试逻辑；超过上限设置 WIFI_FAIL_BIT
//   - GOT_IP 记录 IP 并设置 WIFI_CONNECTED_BIT
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < WIFI_MAX_RETRY) {
      s_retry_num++;
      ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d ...", s_retry_num, WIFI_MAX_RETRY);
      esp_wifi_connect();
    } else {
      ESP_LOGE(TAG, "WiFi connect failed after %d retries", WIFI_MAX_RETRY);
      if (s_wifi_event_group != NULL) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
      }
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    s_ip_addr = event->ip_info.ip.addr;
    ESP_LOGI(TAG, "IP ADDRESS: " IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "http://" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    if (s_wifi_event_group != NULL) {
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
  }
}

// wifi_init：同步尝试连接指定 SSID。成功返回 true，30 s 超时或失败返回 false。
bool wifi_init(const char *ssid, const char *pass) {
  bool result = false;

  // NVS 初始化（可重复调用；页表损坏时自动擦除重建）
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  s_retry_num = 0;

  // 一次性初始化：事件循环 + netif + WiFi 驱动 + 事件处理器注册
  if (!s_wifi_inited) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    // 自定义 hostname：路由器 DHCP / mDNS 客户端列表会显示该名（默认 "espressif"）
    if (sta_netif) {
      esp_netif_set_hostname(sta_netif, "dummy-esp32");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
    s_wifi_inited = true;
  }

  s_wifi_event_group = xEventGroupCreate();

  wifi_config_t c = {.sta = {.threshold = {.authmode = WIFI_AUTH_WPA2_PSK},
                             .pmf_cfg = {.capable = true, .required = false}}};
  snprintf((char *) c.sta.ssid, sizeof(c.sta.ssid), "%s", ssid);
  snprintf((char *) c.sta.password, sizeof(c.sta.password), "%s", pass);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &c));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "WiFi connecting to SSID: %s ...", ssid);

  // 等待 CONNECTED 或 FAIL 事件，上限 30 s（避免死等）
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE,
                                         pdMS_TO_TICKS(30000));

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to SSID: %s", ssid);
    result = true;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Failed to connect to SSID: %s", ssid);
    esp_wifi_stop();
  } else {
    ESP_LOGE(TAG, "WiFi connect timeout");
    esp_wifi_stop();
  }

  // 释放 event group（事件处理器仍注册，以便后续 GOT_IP / DISCONNECTED 事件进入日志）
  vEventGroupDelete(s_wifi_event_group);
  s_wifi_event_group = NULL;
  return result;
}

// wifi_get_ip：将最后获得的 IP 输出为点分十进制字符串；未联网返回空串。
void wifi_get_ip(char *buf, size_t n) {
  if (buf == NULL || n == 0) return;
  if (s_ip_addr == 0) {
    if (n > 0) buf[0] = '\0';
    return;
  }
  uint8_t *p = (uint8_t *) &s_ip_addr;
  snprintf(buf, n, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}
