// ============================================================================
//  main.c —— ESP32 uart-bridge 主入口
// ----------------------------------------------------------------------------
//  启动流程：
//   1. 挂载 SPIFFS（FS_ROOT）
//   2. 读 wifi.json：有则依次尝试；全失败或未配置则进入 CLI 阅读串口命令
//   3. 联网后启动 Mongoose HTTP 服务器（同时提供 WS/TCP/MQTT 桥接）
//   4. 同一主循环内同时驱动 mg_mgr_poll 与 CLI，互不阻塞
//
//  接线：RX = GPIO27，TX = GPIO26（与 dummy 主控板 UART 连接）
// ============================================================================
// Copyright (c) 2020 Cesanta Software Limited
// All rights reserved
#include "main.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "main";

const char *s_listening_url = "http://0.0.0.0:80";

char *config_read(void) {
  return mg_file_read(&mg_fs_posix, FS_ROOT "/config.json", NULL);
}

void config_write(struct mg_str config) {
  mg_file_write(&mg_fs_posix, FS_ROOT "/config.json", config.ptr, config.len);
}

// run_cli：进入纯 CLI 模式（未配置 WiFi 或联网全失败时）。
//   - 此处为阻塞循环，仅响应串口输入；需重启才能退出
static void run_cli(void) {
  MG_INFO(("WiFi is not configured, running CLI. Press enter"));
  cli_init();
  for (;;) {
    uint8_t ch = getchar();
    cli(ch);
    usleep(10000);
  }
}

// app_main：ESP-IDF 应用入口
void app_main(void) {
  // 挂载 SPIFFS，用于读写 WiFi 凭证 / 运行时配置
  esp_vfs_spiffs_conf_t conf = {
      .base_path = FS_ROOT, .max_files = 20, .format_if_mount_failed = true};
  int res = esp_vfs_spiffs_register(&conf);
  ESP_LOGI(TAG, "FS at %s initialised, status: %d", conf.base_path, res);

  // 尝试使用保存的 WiFi 凭证联网（支持多个 SSID 依次尝试）
  char *json = mg_file_read(&mg_fs_posix, WIFI_FILE, NULL);
  if (json != NULL) {
    bool connected = false;
    if (json[0] == '[') {
      // 数组格式：按顺序逐个尝试 [{ssid,pass}, ...]
      for (int i = 0; i < 5; i++) {
        char path[20];
        snprintf(path, sizeof(path), "$[%d].ssid", i);
        char *ssid = mg_json_get_str(mg_str(json), path);
        if (ssid == NULL) break;
        snprintf(path, sizeof(path), "$[%d].pass", i);
        char *pass = mg_json_get_str(mg_str(json), path);
        ESP_LOGI(TAG, "Trying WiFi %d: %s", i + 1, ssid);
        if (wifi_init(ssid, pass ? pass : "")) {
          connected = true;
          free(ssid);
          if (pass) free(pass);
          break;
        }
        free(ssid);
        if (pass) free(pass);
      }
    } else {
      // 历史格式：单个对象 {"ssid":...,"pass":...}
      char *ssid = mg_json_get_str(mg_str(json), "$.ssid");
      char *pass = mg_json_get_str(mg_str(json), "$.pass");
      if (ssid) connected = wifi_init(ssid, pass ? pass : "");
      free(ssid);
      if (pass) free(pass);
    }
    free(json);
    if (!connected) {
      ESP_LOGE(TAG, "All WiFi failed, falling back to CLI");
      run_cli();  // Won't return until reboot
    }
  } else {
    run_cli();  // Won't return until reboot
  }

  // WiFi 已联上，启动 HTTP / WS / TCP / MQTT 服务器
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set(MG_LL_DEBUG);  // Set log level
  ESP_LOGI(TAG, "Mongoose v%s on %s", MG_VERSION, s_listening_url);
  mg_http_listen(&mgr, s_listening_url, uart_bridge_fn, &mgr);

  print_banner();

  // 联网后仍保留 CLI：cli() 是非阻塞的（0/0xFF 直接返回），
  // 可与 mg_mgr_poll 同住在主循环中，不会阻塞网络事件循环。
  // 启发式“重连检测”：串口空闲 ≥ IDLE_RECONNECT_MS 后再收到字节，
  // 视为新终端接入，在处理输入前重新打印 banner。
  cli_init();
  const int64_t IDLE_RECONNECT_MS = 3000;
  int64_t last_rx_ms = esp_timer_get_time() / 1000;
  for (;;) {
    mg_mgr_poll(&mgr, 10);
    int ch = getchar();        // 非阻塞：空闲时返回 EOF (-1)
    if (ch != EOF) {
      int64_t now_ms = esp_timer_get_time() / 1000;
      if (now_ms - last_rx_ms >= IDLE_RECONNECT_MS) {
        print_banner();        // 长时间空闲后重新有输入，推测为重接串口
      }
      last_rx_ms = now_ms;
      cli((uint8_t) ch);
    }
  }
}

// print_banner：在串口控制台打印运行状态概要（IP / HTTP / TCP / WS 地址）。
void print_banner(void) {
  // 去重：空闲重连启发与 `info` 命令可能同时触发，500 ms 内只打一次。
  static int64_t last_banner_ms = 0;
  int64_t now_ms = esp_timer_get_time() / 1000;
  if (now_ms - last_banner_ms < 500) return;
  last_banner_ms = now_ms;

  char ip[16] = {0};
  wifi_get_ip(ip, sizeof(ip));
  printf("\n");
  printf("==================================================\n");
  printf("  Dummy Robot Controller is READY\n");
  printf("--------------------------------------------------\n");
  printf("  Web UI : http://%s/\n",  ip[0] ? ip : "0.0.0.0");
  printf("  TCP    : tcp://%s:4001\n",  ip[0] ? ip : "0.0.0.0");
  printf("  WS     : ws://%s:4002\n",   ip[0] ? ip : "0.0.0.0");
  printf("--------------------------------------------------\n");
  printf("  Type 'help' for CLI commands\n");
  printf("==================================================\n\n");
  fflush(stdout);
}
