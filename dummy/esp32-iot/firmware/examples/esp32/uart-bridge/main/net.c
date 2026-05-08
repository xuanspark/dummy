// ============================================================================
//  net.c —— UART 多路桥接主逻辑（HTTP / WS / TCP / MQTT）
// ----------------------------------------------------------------------------
//  - HTTP 服务静态 web_root + REST 接口 /api/hi /api/config/{get,set}
//  - 4001/TCP 、 4002/WS 、 MQTT broker.hivemq.com:1883 三路可独立开关
//  - 定时器 timer_fn 每 20 ms：检查各侦听器并从 UART 读数据转发给所有客户端
//  - 客户端在 c->data[0] 中打标记： 'W'=WS  'T'=TCP  'M'=MQTT、以区分转发路径
// ============================================================================
// Copyright (c) 2022 Cesanta Software Limited
// All rights reserved

#include "mongoose.h"

#define DEFAULT_TCP "tcp://0.0.0.0:4001"
#define DEFAULT_WEBSOCKET "ws://0.0.0.0:4002"
#define DEFAULT_MQTT "mqtt://broker.hivemq.com:1883?tx=dummy/tx&rx=dummy/rx"

struct endpoint {
  char *url;
  bool enable;
  struct mg_connection *c;
};

struct state {
  struct endpoint tcp, websocket, mqtt;
  int tx, rx, baud;
} s_state = {.tcp = {.enable = true},
             .websocket = {.enable = true},
             .mqtt = {.enable = false},
             .tx = 26,
             .rx = 27,
             .baud = 115200};
static bool s_uart_started = false;

void uart_init(int tx, int rx, int baud);
int uart_read(void *buf, size_t len);
void uart_write(const void *buf, size_t len);
char *config_read(void);
void config_write(struct mg_str config);

// 若外部定义了 UART_API_IMPLEMENTED（如 ESP32 端 uart.c）则使用外部实现，
// 否则使用下面以 stdin/stdout 为虚拟 UART 的桌面仿真版本
#if defined(UART_API_IMPLEMENTED)
#else
void uart_init(int tx, int rx, int baud) {
  // We use stdin/stdout as UART. Make stdin non-blocking
#if MG_ARCH != MG_ARCH_WIN32
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
#endif
  (void) tx, (void) rx, (void) baud;
}

void uart_write(const void *buf, size_t len) {
  fwrite(buf, 1, len, stdout);  // Write to stdout
  fflush(stdout);
  if (strchr(buf, '\n') != NULL) {
    return;
  } else {
    fwrite("\n", 1, 1, stdout);  // Write to stdout
    fflush(stdout);
  }  
}

int uart_read(void *buf, size_t len) {
#if MG_ARCH == MG_ARCH_WIN32
  (void) buf, (void) len;
  return 0;
#else
  return read(0, buf, len);  // Read from stdin
#endif
}

char *config_read(void) {
  return mg_file_read(&mg_fs_posix, "config.json", NULL);
}

void config_write(struct mg_str config) {
  mg_file_write(&mg_fs_posix, "config.json", config.ptr, config.len);
}
#endif

// ws_fn：WebSocket 侦听 / 客户端事件处理
static void ws_fn(struct mg_connection *c, int ev, void *evd, void *fnd) {
  if (ev == MG_EV_HTTP_MSG) {
    mg_ws_upgrade(c, evd, NULL);
  } else if (ev == MG_EV_WS_OPEN) {
    // c->is_hexdumping = 1;
    c->data[0] = 'W';  // When WS handhake is done, mark us as WS client
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *) evd;
    uart_write(wm->data.ptr, wm->data.len);  // Send to UART
    c->recv.len = 0;                         // Discard received data
  } else if (ev == MG_EV_CLOSE) {
    if (c->is_listening) s_state.websocket.c = NULL;
  }
  (void) fnd;
}

// tcp_fn：原生 TCP 侦听 / 客户端事件处理
static void tcp_fn(struct mg_connection *c, int ev, void *evd, void *fnd) {
  if (ev == MG_EV_ACCEPT) {
    // c->is_hexdumping = 1;
    c->data[0] = 'T';  // When client is connected, mark us as TCP client
  } else if (ev == MG_EV_READ) {
    uart_write(c->recv.buf, c->recv.len);  // Send to UART
    c->recv.len = 0;                       // Discard received data
  } else if (ev == MG_EV_CLOSE) {
    if (c->is_listening) s_state.tcp.c = NULL;
  }
  (void) fnd, (void) evd;
}

// mqtt_topic：从 MQTT URL 的 query string（?tx=...&rx=...）中提取主题名
static struct mg_str mqtt_topic(const char *name, const char *dflt) {
  struct mg_str qs = mg_str(strchr(s_state.mqtt.url, '?'));
  struct mg_str v = mg_http_var(qs, mg_str(name));
  return v.ptr == NULL ? mg_str(dflt) : v;
}

// mq_fn：MQTT 连接事件处理：连上后订阅 rx 主题，收到消息写入 UART
static void mq_fn(struct mg_connection *c, int ev, void *evd, void *fnd) {
  if (ev == MG_EV_OPEN) {
    // c->is_hexdumping = 1;
  } else if (ev == MG_EV_MQTT_OPEN) {
    c->data[0] = 'M';
    struct mg_mqtt_opts sub_opts;
    memset(&sub_opts, 0, sizeof(sub_opts));
    sub_opts.topic = mqtt_topic("rx", "b/rx");
    sub_opts.qos = 1;
    mg_mqtt_sub(c, &sub_opts);  // Subscribe to RX topic
  } else if (ev == MG_EV_MQTT_MSG) {
    struct mg_mqtt_message *mm = evd;        // MQTT message
    uart_write(mm->data.ptr, mm->data.len);  // Send to UART
  } else if (ev == MG_EV_CLOSE) {
    s_state.mqtt.c = NULL;
  }
  (void) fnd, (void) evd;
}

// timer_fn：软件定时器。频率接近 mg_mgr_poll 的微人加，负责：
//   1. 保证 enable=true 的侦听器 / MQTT 连接始终存在（关闭后自动重拉起）
//   2. 从 UART 拉取一批数据，广播给所有 WS / TCP / MQTT 客户端
static void timer_fn(void *param) {
  // 若某路侦听器被关闭，重新拉起（config_apply 会 is_draining 旧连接）
  struct mg_mgr *mgr = (struct mg_mgr *) param;
  if (s_state.tcp.c == NULL && s_state.tcp.enable) {
    s_state.tcp.c = mg_listen(mgr, s_state.tcp.url, tcp_fn, 0);
  }
  if (s_state.websocket.c == NULL && s_state.websocket.enable) {
    s_state.websocket.c = mg_http_listen(mgr, s_state.websocket.url, ws_fn, 0);
  }
  if (s_state.mqtt.c == NULL && s_state.mqtt.enable) {
    struct mg_mqtt_opts opts = {.clean = true};
    s_state.mqtt.c = mg_mqtt_connect(mgr, s_state.mqtt.url, &opts, mq_fn, 0);
  }

  // 从 UART 读数据
  char buf[512];
  int len = uart_read(buf, sizeof(buf));
  if (len > 0) {
    // 遍历所有连接，按之前打的 W/T/M 标记广播转发
    for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
      if (c->data[0] == 'W') mg_ws_send(c, buf, len, WEBSOCKET_OP_TEXT);
      if (c->data[0] == 'T') mg_send(c, buf, len);
      if (c->data[0] == 'M') {
        struct mg_mqtt_opts pub_opts;
        memset(&pub_opts, 0, sizeof(pub_opts));
        pub_opts.topic = mqtt_topic("tx", "dummy/tx");
        pub_opts.message = mg_str_n(buf, len);
        pub_opts.qos = 1, pub_opts.retain = false;
        mg_mqtt_pub(c, &pub_opts);
      }
    }
  }
}

static void update_string(struct mg_str json, const char *path, char **value) {
  char *jval;
  if ((jval = mg_json_get_str(json, path)) != NULL) {
    free(*value);
    *value = strdup(jval);
    free(jval);
  }
}

static void config_apply(struct mg_str s) {
  MG_INFO(("Applying config: %.*s", (int) s.len, s.ptr));
  int old_tx = s_state.tx, old_rx = s_state.rx, old_baud = s_state.baud;

  bool b;
  if (mg_json_get_bool(s, "$.tcp.enable", &b)) s_state.tcp.enable = b;
  if (mg_json_get_bool(s, "$.ws.enable", &b)) s_state.websocket.enable = b;
  if (mg_json_get_bool(s, "$.mqtt.enable", &b)) s_state.mqtt.enable = b;

  update_string(s, "$.tcp.url", &s_state.tcp.url);
  update_string(s, "$.mqtt.url", &s_state.mqtt.url);
  update_string(s, "$.ws.url", &s_state.websocket.url);

  double v;
  if (mg_json_get_num(s, "$.rx", &v)) s_state.rx = (int) v;
  if (mg_json_get_num(s, "$.tx", &v)) s_state.tx = (int) v;
  if (mg_json_get_num(s, "$.baud", &v)) s_state.baud = (int) v;

  if (s_uart_started &&
      (old_tx != s_state.tx || old_rx != s_state.rx || old_baud != s_state.baud)) {
    uart_init(s_state.tx, s_state.rx, s_state.baud);
  }

  if (s_state.mqtt.c) s_state.mqtt.c->is_draining = 1;
  if (s_state.tcp.c) s_state.tcp.c->is_draining = 1;
  if (s_state.websocket.c) s_state.websocket.c->is_draining = 1;
}

// uart_bridge_fn：HTTP 服务器主入口 + 初始化入口。
//   - 侦听器 OPEN 时加载配置、创建 20ms 轮询定时器、初始化 UART
//   - 提供 /api/hi (心跳) /api/config/get /api/config/set 三个 REST 接口
//   - 其余 URL 走 web_root 静态资源（优先从 packed_fs 读取）
void uart_bridge_fn(struct mg_connection *c, int ev, void *ev_data,
                    void *fn_data) {
  if (ev == MG_EV_OPEN && c->is_listening) {
    s_state.tcp.url = strdup(DEFAULT_TCP);
    s_state.websocket.url = strdup(DEFAULT_WEBSOCKET);
    s_state.mqtt.url = strdup(DEFAULT_MQTT);
    char *config = config_read();
    if (config != NULL) config_apply(mg_str(config));
    free(config);
    mg_timer_add(c->mgr, 20, MG_TIMER_REPEAT, timer_fn, c->mgr);
    uart_init(s_state.tx, s_state.rx, s_state.baud);
    s_uart_started = true;
    // mg_log_set(MG_LL_DEBUG);                  // Set log level
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    if (mg_http_match_uri(hm, "/api/hi")) {
      mg_http_reply(c, 200, "", "hi\n");  // Testing endpoint
    } else if (mg_http_match_uri(hm, "/api/config/set")) {
      config_apply(hm->body);
      config_write(hm->body);
      mg_http_reply(c, 200, "", "true\n");
    } else if (mg_http_match_uri(hm, "/api/config/get")) {
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{%m:{%m:%m,%m:%s},%m:{%m:%m,%m:%s},%m:{%m:%m,%m:%s},"
                    "%m:%d,%m:%d,%m:%d}\n",
                    mg_print_esc, 0, "tcp", mg_print_esc, 0, "url",
                    mg_print_esc, 0, s_state.tcp.url, mg_print_esc, 0, "enable",
                    s_state.tcp.enable ? "true" : "false", mg_print_esc, 0,
                    "ws", mg_print_esc, 0, "url", mg_print_esc, 0,
                    s_state.websocket.url, mg_print_esc, 0, "enable",
                    s_state.websocket.enable ? "true" : "false", mg_print_esc,
                    0, "mqtt", mg_print_esc, 0, "url", mg_print_esc, 0,
                    s_state.mqtt.url, mg_print_esc, 0, "enable",
                    s_state.mqtt.enable ? "true" : "false", mg_print_esc, 0,
                    "rx", s_state.rx, mg_print_esc, 0, "tx", s_state.tx,
                    mg_print_esc, 0, "baud", s_state.baud);
    } else {
      struct mg_http_serve_opts opts;
      memset(&opts, 0, sizeof(opts));
#if 1
      opts.root_dir = "/web_root";
      opts.fs = &mg_fs_packed;
#else
      opts.root_dir = "web_root";
#endif
      mg_http_serve_dir(c, ev_data, &opts);
    }
  }
  (void) fn_data;
}
