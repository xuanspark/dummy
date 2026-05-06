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
static bool s_wifi_inited = false;  // track one-time init
static uint32_t s_ip_addr = 0;       // last obtained IP (network byte order)

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < WIFI_MAX_RETRY) {
      s_retry_num++;
      ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d ...", s_retry_num, WIFI_MAX_RETRY);
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_wifi_connect();
    } else {
      ESP_LOGE(TAG, "WiFi connect failed after %d retries", WIFI_MAX_RETRY);
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    s_ip_addr = event->ip_info.ip.addr;
    ESP_LOGI(TAG, "IP ADDRESS: " IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "http://" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

bool wifi_init(const char *ssid, const char *pass) {
  bool result = false;

  // NVS init (safe to call multiple times)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  s_retry_num = 0;

  // One-time init: event loop, netif, wifi driver, event handlers
  if (!s_wifi_inited) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

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

  // Wait with 30s timeout instead of forever
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

  // Clean up event group (handlers stay registered)
  vEventGroupDelete(s_wifi_event_group);
  s_wifi_event_group = NULL;
  return result;
}

// Returns IP in dotted-quad form (e.g. "192.168.1.42") or empty string if none.
void wifi_get_ip(char *buf, size_t n) {
  if (buf == NULL || n == 0) return;
  if (s_ip_addr == 0) {
    if (n > 0) buf[0] = '\0';
    return;
  }
  uint8_t *p = (uint8_t *) &s_ip_addr;
  snprintf(buf, n, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}
