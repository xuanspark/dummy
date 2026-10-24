// ============================================================================
//  interface_can.hpp —— CAN 通道（CAN1/CAN2）公共接口
// ----------------------------------------------------------------------------
//  - 暴露 CAN_context 结构、CanSendMessage、get_can_ctx、信号量句柄等
//  - 实现细节见 interface_can.cpp（含本轮修复的 tx_error / 100ms 超时等）
// ============================================================================
#ifndef __INTERFACE_CAN_HPP
#define __INTERFACE_CAN_HPP

#include "fibre/protocol.hpp"
#include <stm32f4xx_hal.h>
#include <cmsis_os.h>

// CAN 总线上下文：每条 CAN 接口（CAN1/CAN2）一份，存放 HAL 句柄、本机节点 ID、
// 心跳信号量、回调统计计数等运行期状态。
struct CAN_context
{
    CAN_HandleTypeDef* handle = nullptr;     // HAL CAN 句柄（CAN1 或 CAN2）
    uint8_t node_id = 0;                     // 本机在该总线上的节点 ID（0 = 广播）
    uint64_t serial_number = 0;              // 用于动态分配 node_id 的板序列号

    // node_ids_in_use_*：滑动窗口位图（128 bit），用于动态 ID 分配避免冲突
    uint32_t node_ids_in_use_0[4]; // 128 bits (indicate if a node ID was in use up to 1 second ago)
    uint32_t node_ids_in_use_1[4]; // 128 bits (indicats if a node ID was in use 1-2 seconds ago)

    uint32_t last_heartbeat_mailbox = 0;     // 最近一次心跳占用的 TX 邮箱号
    uint32_t tx_msg_cnt = 0;                 // 累计成功发送计数

    uint8_t node_id_rng_state = 0;           // 节点 ID 随机分配状态机

    osSemaphoreId_t sem_send_heartbeat;      // 心跳节拍信号量（由定时任务释放）

    // ---- 各类 HAL 回调命中统计（debug / 在线诊断用） ----
    uint32_t TxMailboxCompleteCallbackCnt = 0;   // TX 完成命中次数
    uint32_t TxMailboxAbortCallbackCnt = 0;      // TX abort 命中次数
    int RxFifo0MsgPendingCallbackCnt = 0;        // FIFO0 有新消息
    int RxFifo0FullCallbackCnt = 0;              // FIFO0 满
    int RxFifo1MsgPendingCallbackCnt = 0;        // FIFO1 有新消息
    int RxFifo1FullCallbackCnt = 0;              // FIFO1 满
    int SleepCallbackCnt = 0;                    // CAN 进入睡眠
    int WakeUpFromRxMsgCallbackCnt = 0;          // 由 RX 唤醒
    int ErrorCallbackCnt = 0;                    // ErrorCallback 命中次数

    uint32_t received_msg_cnt = 0;               // 收到的总消息数
    uint32_t received_ack = 0;                   // 收到的 ACK 计数
    uint32_t unexpected_errors = 0;              // 未预期错误（用于排障）
    uint32_t unhandled_messages = 0;             // 收到了但无人处理的消息
};

// 由 HAL CAN 句柄反查上下文（CAN1 → ctx[0]、CAN2 → ctx[1]）。
struct CAN_context* get_can_ctx(CAN_HandleTypeDef* hcan);

// 启动指定 CAN 总线的服务：开过滤器 / 起 RX 中断 / 起心跳任务等。
// 由 communication.cpp 的 CommunicationTask 在 publish 完毕后依次调用。
bool StartCanServer(CAN_TypeDef* hcan);

// 标准 8 字节 CAN 帧发送。带 100 ms 信号量超时，超时静默放弃。
// txHeader.StdId 由调用方设好，本函数仅负责 TX 邮箱占用与超时管理。
void CanSendMessage(CAN_context* canCtx, uint8_t* txData, CAN_TxHeaderTypeDef* txHeader);

// CAN RX 顶层回调入口：communication 层将 HAL 中断结果转交此处，
// 实现见 UserApp/protocols/can_protocol.cpp，按 StdId 分发到具体节点。
void OnCanMessage(CAN_context* canCtx, CAN_RxHeaderTypeDef* rxHeader, uint8_t* data);

#endif // __INTERFACE_CAN_HPP —— 头文件包含守卫结束
