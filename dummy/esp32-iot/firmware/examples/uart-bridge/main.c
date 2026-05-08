// ============================================================================
//  main.c —— uart-bridge 桌面仿真版主入口（Linux/macOS/Windows）
// ----------------------------------------------------------------------------
//  - 真实部署在 ESP32 上，业务实现见 ../esp32/uart-bridge/main/
//  - 桌面版用 stdin/stdout 模拟 UART，便于在本地调试 net.c 的 HTTP/WS/TCP/MQTT 逻辑
//  - 监听端口 8000（HTTP）、4001（TCP）、4002（WS）
// ============================================================================
// Copyright (c) 2022 Cesanta Software Limited
// All rights reserved

#include "mongoose.h"
const char *s_listening_url = "http://0.0.0.0:8000";

void uart_bridge_fn(struct mg_connection *, int, void *, void *);

int main(void) {
  struct mg_mgr mgr;
  mg_log_set(MG_LL_DEBUG);
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, s_listening_url, uart_bridge_fn, &mgr);
  MG_INFO(("Listening on %s", s_listening_url));
  while (mgr.conns != NULL) mg_mgr_poll(&mgr, 500);
  mg_mgr_free(&mgr);
  return 0;
}
