// ============================================================================
//  interface_usb.hpp —— USB-CDC 通道公共接口
// ----------------------------------------------------------------------------
//  - 暴露 usbStreamOutputPtr（StreamSink）供 Respond 等使用
//  - 内部 USBSender 封装了 CDC_Transmit_FS + sem_usb_tx 信号量保护
//  - 实现细节见 interface_usb.cpp（含本轮修复的 100ms 超时与"超时不强发"策略）
// ============================================================================
#ifndef __INTERFACE_USB_HPP
#define __INTERFACE_USB_HPP

// --- C++ 上下文专用部分 ---
#ifdef __cplusplus

#include "fibre/protocol.hpp"

// 上层通过该指针 Respond / printf 重定向到 USB CDC；
// 实体在 interface_usb.cpp 以 USBSender 的形式静态构造。
extern StreamSink *usbStreamOutputPtr;

// 进入 C 链接区，让 freertos.c 等 C 文件也能调下面的接口
extern "C" {
#endif

#include <cmsis_os.h>
#include <stdint.h>

// USB 运行期统计（供在线诊断 / 设备路径质量评估）
typedef struct
{
    uint32_t rx_cnt;            // 总接收字节计数
    uint32_t tx_cnt;            // 总发送字节计数
    uint32_t tx_overrun_cnt;    // TX 超时放弃计数（本轮修复后不会再在超时后强发）
} USBStats_t;

extern USBStats_t usb_stats_;

// USB CDC RX 回调入口：HAL 的 CDC_Receive_FS 里会调本函数。
// endpoint_pair 区分 IN/OUT 以便统计，双以上 USB 实例时也用。
void usb_rx_process_packet(uint8_t *buf, uint32_t len, uint8_t endpoint_pair);

// 启动 USB 服务：绑定 StreamSink、起 USB 处理线程。由 communication.cpp 调。
void StartUsbServer(void);

#ifdef __cplusplus
}
#endif

#endif // __INTERFACE_USB_HPP —— 头文件包含守卫结束
