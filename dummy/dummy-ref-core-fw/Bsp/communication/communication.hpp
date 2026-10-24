// ============================================================================
//  communication.hpp —— 通信子系统总入口
// ----------------------------------------------------------------------------
//  - 聚合 USB-CDC、UART、CAN 三条物理通道的初始化与协议绑定
//  - 提供 fibre 协议发布的 COMMIT_PROTOCOL 宏（基于 MakeObjTree() 模板树）
//  - InitCommunication() 由 Main() 启动序列首先调用
// ============================================================================
#ifndef COMMANDS_H
#define COMMANDS_H

// --- C 与 C++ 都需要能看见的接口 ---
// CubeMX 生成的 freertos.c（C 文件）也会 #include 本头文件，
// 所以这些函数必须退出 C++ 名字修饰才能被两边同时调用。
#ifdef __cplusplus
extern "C" {
#endif

#include <cmsis_os.h>

void InitCommunication(void);                      // 初始化所有物理通道（USB / UART / CAN）
void CommitProtocol();                             // 把对象树发布给 Fibre 远程协议
void CommunicationTask(void *ctx);                 // 通信任务入口（FreeRTOS）
void UsbDeferredInterruptTask(void *ctx);          // USB 中断延迟处理任务

#ifdef __cplusplus
}

// --- 以下仅在 C++ 上下文可见（模板 / fibre 主体） ---
#include <functional>
#include <limits>
#include "ascii_processor.hpp"
#include "interface_usb.hpp"
#include "interface_uart.hpp"
#include "interface_can.hpp"

// COMMIT_PROTOCOL——在 .cpp 里一锁定进行 “对象树 → fibre_publish”。
// 使用者在某个 .cpp 文件中调一次该宏即可，生成的 CommitProtocol()
// 会调 MakeObjTree() 拼出顶层对象树，然后交给 fibre_publish 发布。
//   - treeType : 推导出的对象树类型
//   - treeBuffer：静态存储，避免入栈超限（树可能 ≥1 KB）
//   - placement new 在该缓冲区构造对象，然后 fibre_publish 拿引用
#define COMMIT_PROTOCOL \
using treeType = decltype(MakeObjTree());\
uint8_t treeBuffer[sizeof(treeType)];\
void CommitProtocol()\
{\
    auto treePtr = new(treeBuffer) treeType(MakeObjTree());\
    fibre_publish(*treePtr);\
}\


#endif // __cplusplus
#endif /* COMMANDS_H */
