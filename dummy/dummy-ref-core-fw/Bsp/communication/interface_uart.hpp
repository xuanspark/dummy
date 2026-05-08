// ============================================================================
//  interface_uart.hpp —— UART 通道（UART4 / UART5）的发送/接收接口
// ----------------------------------------------------------------------------
//  - 提供 uart4StreamOutputPtr / uart5StreamOutputPtr 两个 StreamSink 句柄
//  - 接收侧由 HAL UART RX 中断驱动，按 \n 切分后转交 OnUartXAsciiCmd
//  - 信号量保护 TX，避免线程间撞包
// ============================================================================
#ifndef __INTERFACE_UART_HPP
#define __INTERFACE_UART_HPP

// --- C++ 上下文专用部分（StreamSink 是 C++ 类型，C 看不到） ---
#ifdef __cplusplus

#include "fibre/protocol.hpp"

// 上层（Respond / printf 重定向 / 协议层）通过这两个指针向 UART 输出。
// 实际对象在 interface_uart.cpp 中以 UARTSender 派生类的形式构造。
extern StreamSink *uart4StreamOutputPtr;   // UART4：默认调试通道，与 USB-CDC 同步接收 printf
extern StreamSink *uart5StreamOutputPtr;   // UART5：扩展通道（部分协议命令绑定于此）

// 进入 C 链接区，让下面的声明 freertos.c 等纯 C 文件也能可见
extern "C" {
#endif

#include <cmsis_os.h>

extern osThreadId uart_thread;             // UART RX 服务线程句柄（在 .cpp 内创建）

// 启动 UART 服务：开 DMA 双缓冲循环 RX、注册流对象、起 1 ms 轮询线程。
// 由 communication.cpp 的 CommunicationTask 在 publish 完毕后调用。
void StartUartServer(void);

#ifdef __cplusplus
}
#endif

#endif // __INTERFACE_UART_HPP —— 头文件包含守卫结束
