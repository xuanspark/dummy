// ============================================================================
//  uart.c —— ESP-IDF UART1 驱动封装
// ----------------------------------------------------------------------------
//  - 为 net.c 提供 uart_init / uart_read / uart_write 三个业务接口
//  - 默认 8N1、无流控，收发 FIFO 各 SOC_UART_FIFO_LEN*2
//  - 发送末尾自动补 "\r\n"（使主控板 ASCII 协议按行解析）
// ============================================================================
#include "main.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static const char *TAG = "uart";

int uart_close(int no) {
  return uart_driver_delete(no);
}

// uart_open：配置并安装 UART 驱动。cts/rts 传 -1 表示不使用硬件流控。
int uart_open(int no, int rx, int tx, int cts, int rts, int baud) {
  uart_config_t uart_config = {
      .baud_rate = baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = cts > 0 && rts > 0 ? UART_HW_FLOWCTRL_CTS_RTS
                   : cts > 0          ? UART_HW_FLOWCTRL_CTS
                   : rts > 0          ? UART_HW_FLOWCTRL_RTS
                                      : UART_HW_FLOWCTRL_DISABLE,
  };
  int e1 = uart_param_config(no, &uart_config);
  int e2 = uart_set_pin(no, tx, rx, rts, cts);
  int e3 =
      uart_driver_install(no, SOC_UART_FIFO_LEN * 2, SOC_UART_FIFO_LEN * 2, 0, NULL, 0);
  ESP_LOGI(TAG, "%d: %d/%d/%d, %d %d %d", no, rx, tx, baud, e1, e2, e3);
  if (e1 != ESP_OK || e2 != ESP_OK || e3 != ESP_OK) return -1;
  return no;
}

// uart_init：bridge 业务调用的入口，固定使用 UART_NO（UART1）不启用流控。
void uart_init(int tx, int rx, int baud) {
  uart_driver_delete(UART_NO);
  uart_open(UART_NO, rx, tx, -1, -1, baud);
}

// uart_read：从 UART1 FIFO 读取。无数据时立即返回 0；有数据时最多等待 10 ms。
int uart_read(void *buf, size_t len) {
  size_t x = 0;
  int no = UART_NO;
  if (uart_get_buffered_data_len(no, &x) != ESP_OK || x == 0) return 0;
  int n = uart_read_bytes(no, buf, len, 10 / portTICK_PERIOD_MS);
  ESP_LOGD(TAG, "%d bytes: [%.*s]", n, n, (char *) buf);
  return n;
}

// uart_write：向 UART1 发送一段字节。若该段中未含 '\n'，末尾补一个 "\r\n"。
int uart_write(const void *buf, size_t len) {
  int no = UART_NO;
  int ret;

  ESP_LOGD(TAG, "%lu bytes: [%.*s]", (unsigned long) len, (int) len, (char *) buf);
  ret = uart_write_bytes(no, (const char *) buf, len);

  // 使用 memchr 而非 strchr，避免对未 null 结尾的网络缓冲越界读
  if (memchr(buf, '\n', len) == NULL) {
    ESP_LOGD(TAG, "Append \\r\\n to end");
    uart_write_bytes(no, "\r\n", 2);
    return ret + 2;
  }
  return ret;
}
