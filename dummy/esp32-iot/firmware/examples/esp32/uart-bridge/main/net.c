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

// ---- T08: 配置接口鉴权与维护模式 ----
#define CONFIG_AUTH_TOKEN "dummy-maint-2024"  // 生产环境应从 NVS 读取或使用更安全的方案
static bool s_maintenance_mode = false;  // 维护模式：仅开启时允许修改 UART 引脚/baud

// 验证 HTTP 请求中的 Authorization: Bearer <token>
static bool check_auth(struct mg_http_message *hm) {
  struct mg_str *auth = mg_http_get_header(hm, "Authorization");
  if (auth == NULL) return false;
  struct mg_str expected = mg_str("Bearer " CONFIG_AUTH_TOKEN);
  return mg_strcmp(*auth, expected) == 0;
}

// baud 率白名单
static const int s_valid_bauds[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static bool is_valid_baud(int baud) {
  for (size_t i = 0; i < sizeof(s_valid_bauds)/sizeof(s_valid_bauds[0]); i++)
    if (s_valid_bauds[i] == baud) return true;
  return false;
}

// ---- T09: UART 写入网关 —— 长度、字符集、频率限制 ----
#define UART_LINE_MAX   63   // 与 ref-core 队列消息体一致（含 \0 = 64）
#define UART_RATE_MS    20   // 最小命令间隔（ms），防止 burst

static uint64_t s_last_uart_write_ms = 0;

// 检查 buf 是否全部为可打印 ASCII（0x20~0x7E）+ \r + \n
static bool is_printable_ascii(const char *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)buf[i];
    if (ch >= 0x20 && ch <= 0x7E) continue;  // 可打印
    if (ch == '\r' || ch == '\n') continue;   // 行终止符
    return false;
  }
  return true;
}

// 受保护的 UART 写入：拒绝超长、非法字符、高频命令
static bool uart_write_validated(const void *buf, size_t len) {
  if (len == 0) return false;
  // 长度限制
  if (len > UART_LINE_MAX) {
    MG_ERROR(("UART reject: too long (%lu > %d)", (unsigned long)len, UART_LINE_MAX));
    return false;
  }
  // 字符集限制
  if (!is_printable_ascii((const char *)buf, len)) {
    MG_ERROR(("UART reject: non-printable chars"));
    return false;
  }
  // 频率限制
  uint64_t now = mg_millis();
  if (now - s_last_uart_write_ms < UART_RATE_MS) {
    MG_ERROR(("UART reject: rate limited"));
    return false;
  }
  s_last_uart_write_ms = now;
  uart_write(buf, len);
  return true;
}

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
    uart_write_validated(wm->data.ptr, wm->data.len);  // Send to UART (validated)
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
    uart_write_validated(c->recv.buf, c->recv.len);  // Send to UART (validated)
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
    uart_write_validated(mm->data.ptr, mm->data.len);  // Send to UART (validated)
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
      // T08: 鉴权检查
      if (!check_auth(hm)) {
        mg_http_reply(c, 401, "", "{%m:%m}\n", mg_print_esc, 0, "error",
                      mg_print_esc, 0, "unauthorized");
      } else {
        // T08: UART 引脚/baud 修改需要维护模式
        double v;
        bool wants_uart_change = false;
        if (mg_json_get_num(hm->body, "$.rx", &v) ||
            mg_json_get_num(hm->body, "$.tx", &v) ||
            mg_json_get_num(hm->body, "$.baud", &v))
          wants_uart_change = true;
        if (wants_uart_change && !s_maintenance_mode) {
          mg_http_reply(c, 403, "", "{%m:%m}\n", mg_print_esc, 0, "error",
                        mg_print_esc, 0, "uart config requires maintenance mode");
        } else {
          // T08: baud 白名单校验
          double baud_val;
          if (mg_json_get_num(hm->body, "$.baud", &baud_val) && !is_valid_baud((int)baud_val)) {
            mg_http_reply(c, 400, "", "{%m:%m}\n", mg_print_esc, 0, "error",
                          mg_print_esc, 0, "invalid baud rate");
          } else {
            config_apply(hm->body);
            config_write(hm->body);
            mg_http_reply(c, 200, "", "true\n");
          }
        }
      }
    } else if (mg_http_match_uri(hm, "/api/maintenance")) {
      // T08: 维护模式开关（需鉴权）
      if (!check_auth(hm)) {
        mg_http_reply(c, 401, "", "{%m:%m}\n", mg_print_esc, 0, "error",
                      mg_print_esc, 0, "unauthorized");
      } else {
        bool enable = false;
        mg_json_get_bool(hm->body, "$.enable", &enable);
        s_maintenance_mode = enable;
        MG_INFO(("Maintenance mode: %s", enable ? "ON" : "OFF"));
        mg_http_reply(c, 200, "", "{%m:%s}\n", mg_print_esc, 0, "maintenance",
                      enable ? "true" : "false");
      }
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
