// Copyright (c) 2020 Cesanta Software Limited
// All rights reserved
//gpio27 RX
//gpio26 TX
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

static void run_cli(void) {
  MG_INFO(("WiFi is not configured, running CLI. Press enter"));
  cli_init();
  for (;;) {
    uint8_t ch = getchar();
    cli(ch);
    usleep(10000);
  }
}

void app_main(void) {
  // Mount filesystem
  esp_vfs_spiffs_conf_t conf = {
      .base_path = FS_ROOT, .max_files = 20, .format_if_mount_failed = true};
  int res = esp_vfs_spiffs_register(&conf);
  ESP_LOGI(TAG, "FS at %s initialised, status: %d", conf.base_path, res);

  // Try to connect to wifi by using saved WiFi credentials
  char *json = mg_file_read(&mg_fs_posix, WIFI_FILE, NULL);
  if (json != NULL) {
    bool connected = false;
    if (json[0] == '[') {
      // Array format: try each WiFi in order
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
      // Legacy single object format
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

  // Connected to WiFi, now start HTTP server
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set(MG_LL_DEBUG);  // Set log level
  ESP_LOGI(TAG, "Mongoose v%s on %s", MG_VERSION, s_listening_url);
  mg_http_listen(&mgr, s_listening_url, uart_bridge_fn, &mgr);

  print_banner();

  // Make CLI available while WiFi is online too. cli() is non-blocking
  // (returns immediately on 0/0xFF), so we can drive it from the same loop
  // that polls Mongoose without ever blocking the network event loop.
  // Heuristic "reconnect detection": if the serial line has been idle for
  // >= IDLE_RECONNECT_MS and a new byte arrives, treat it as a new terminal
  // attaching and re-print the banner before processing the input.
  cli_init();
  const int64_t IDLE_RECONNECT_MS = 3000;
  int64_t last_rx_ms = esp_timer_get_time() / 1000;
  for (;;) {
    mg_mgr_poll(&mgr, 10);
    int ch = getchar();        // non-blocking: returns EOF (-1) when idle
    if (ch != EOF) {
      int64_t now_ms = esp_timer_get_time() / 1000;
      if (now_ms - last_rx_ms >= IDLE_RECONNECT_MS) {
        print_banner();        // serial reconnected after a quiet gap
      }
      last_rx_ms = now_ms;
      cli((uint8_t) ch);
    }
  }
}

void print_banner(void) {
  // Dedupe: when both the idle-reconnect heuristic and the explicit `info`
  // command fire from a single user input, only print once.
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
