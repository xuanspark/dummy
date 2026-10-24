/*
* 零配置节点 ID 协商
* -------------------------------
*
* 心跳报文：负载为 8 字节唯一序列号的报文。
* 普通报文：除心跳之外的任何报文。
*
* 所有节点必须遵守以下四条规则：
*
* a) 某个时刻节点必须认为某 ID 已被其他节点占用，如果满足以下任一条件：
*    - 在过去 1 秒内收到了带该 ID 且非本机发送的心跳报文
*    - 在过去 1 秒内本机尝试发送该 ID 的心跳报文但失败（未被 ACK）
*
* b) 某个时刻节点必须认为某 ID 未被本机拥有，如果：
*    在过去 1 秒内本机未成功发送过该 ID 的心跳报文。
*
* c) 某个时刻节点不得发送带被占用 ID 的心跳报文。
*
* d) 某个时刻节点不得发送带未本机拥有 ID 的普通报文。
*
* 硬件资源分配
* -------------------
*   RX FIFO0：
*       - filter bank 0：心跳报文
*/

#include "common_inc.h"
#include <stm32f4xx_hal.h>
#include <cmsis_os.h>

// 定义于 CubeMX 生成的 can.c
extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

CAN_context can1Ctx;
CAN_context can2Ctx;
static CAN_context* ctxs = nullptr;
static CAN_RxHeaderTypeDef headerRx;
static uint8_t data[8];


struct CAN_context* get_can_ctx(CAN_HandleTypeDef* hcan)
{
    if (hcan->Instance == CAN1)
        return &can1Ctx;
    else if (hcan->Instance == CAN2)
        return &can2Ctx;
    else
        return nullptr;
}

bool StartCanServer(CAN_TypeDef* hcan)
{
    if (hcan == CAN1)
    {
        ctxs = &can1Ctx;
        ctxs->handle = &hcan1;
    } else if (hcan == CAN2)
    {
        ctxs = &can2Ctx;
        ctxs->handle = &hcan2;
    } else
        return false; // 上面两个判断都不命中，返回失败

    HAL_StatusTypeDef status;

    ctxs->node_id = 0;
    ctxs->serial_number = serialNumber;
    osSemaphoreDef(sem_send_heartbeat);
    ctxs->sem_send_heartbeat = osSemaphoreNew(1, 0, osSemaphore(sem_send_heartbeat));

    //// 设置接收过滤器（全接受）
    CAN_FilterTypeDef sFilterConfig = {
        .FilterIdHigh = 0x0000,
        .FilterIdLow = 0x0000,
        .FilterMaskIdHigh = 0x0000,
        .FilterMaskIdLow = 0x0000,
        .FilterFIFOAssignment = CAN_RX_FIFO0,
        .FilterBank = 0,
        .FilterMode = CAN_FILTERMODE_IDMASK,
        .FilterScale = CAN_FILTERSCALE_16BIT, // 两个 16-bit 过滤器
        .FilterActivation = ENABLE,
        .SlaveStartFilterBank = 0
    };
    status = HAL_CAN_ConfigFilter(ctxs->handle, &sFilterConfig);
    if (status != HAL_OK)
        return false;

    status = HAL_CAN_Start(ctxs->handle);
    if (status != HAL_OK)
        return false;

    status = HAL_CAN_ActivateNotification(ctxs->handle,
                                          CAN_IT_TX_MAILBOX_EMPTY |
                                          CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO1_MSG_PENDING |
                                          /* 实际主要依赖上面两项 "MSG_PENDING"，以下是安全冷备 */
                                          CAN_IT_RX_FIFO0_FULL | CAN_IT_RX_FIFO1_FULL |
                                          CAN_IT_RX_FIFO0_OVERRUN | CAN_IT_RX_FIFO1_OVERRUN |
                                          CAN_IT_WAKEUP | CAN_IT_SLEEP_ACK |
                                          CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE |
                                          CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE |
                                          CAN_IT_ERROR);
    if (status != HAL_OK)
        return false;

    return true;
}

void tx_complete_callback(CAN_HandleTypeDef* hcan, uint8_t mailbox_idx)
{
//    CAN_context* ctx = get_can_ctx(hcan);
//    if (!ctx) return;
//    ctx->tx_msg_cnt++;

    if (hcan->Instance == CAN1)
        osSemaphoreRelease(sem_can1_tx);
    else if (hcan->Instance == CAN2)
        osSemaphoreRelease(sem_can2_tx);
}

void tx_aborted_callback(CAN_HandleTypeDef* hcan, uint8_t mailbox_idx)
{
    if (!get_can_ctx(hcan))
        return;
    get_can_ctx(hcan)->TxMailboxAbortCallbackCnt++;

    // 修复：abort 同样意味着邮箱不再持有该消息，必须释放对应 TX 信号量，
    // 否则 CanSendMessage 中的 osSemaphoreAcquire 会永久阻塞 ASCII 处理线程。
    if (hcan->Instance == CAN1)
        osSemaphoreRelease(sem_can1_tx);
    else if (hcan->Instance == CAN2)
        osSemaphoreRelease(sem_can2_tx);
}

void tx_error(CAN_context* ctx, uint8_t mailbox_idx)
{
    // 修复：原本是空函数。CAN 总线上无节点应答时（例如 REF 板单飞），
    // HAL_CAN_ErrorCallback 走 TX_TERR{0,1,2} 分支调到这里 —— 必须把
    // 对应 CAN TX 信号量释放，否则配对的 osSemaphoreAcquire 永远等不到回调，
    // 会拖死整个 ASCII 命令处理线程，使 #ONLINE / #GET_DCE 等触 CAN 命令把
    // 后续所有命令拖累超时（host-pc 端表现为"中毒"）。
    if (!ctx)
        return;
    if (ctx->handle->Instance == CAN1)
        osSemaphoreRelease(sem_can1_tx);
    else if (ctx->handle->Instance == CAN2)
        osSemaphoreRelease(sem_can2_tx);
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef* hcan)
{ tx_complete_callback(hcan, 0); }

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef* hcan)
{ tx_complete_callback(hcan, 1); }

void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef* hcan)
{ tx_complete_callback(hcan, 2); }

void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef* hcan)
{ tx_aborted_callback(hcan, 0); }

void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef* hcan)
{ tx_aborted_callback(hcan, 1); }

void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef* hcan)
{ tx_aborted_callback(hcan, 2); }

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
    CAN_context* ctx = get_can_ctx(hcan);
    if (!ctx) return;
    ctx->received_msg_cnt++;

    HAL_StatusTypeDef status = HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &headerRx, data);
    if (status != HAL_OK)
    {
        ctx->unexpected_errors++;
        return;
    }

    OnCanMessage(ctx, &headerRx, data);
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef* hcan)
{ if (get_can_ctx(hcan)) get_can_ctx(hcan)->RxFifo0FullCallbackCnt++; }

void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef* hcan)
{ if (get_can_ctx(hcan)) get_can_ctx(hcan)->RxFifo1MsgPendingCallbackCnt++; }

void HAL_CAN_RxFifo1FullCallback(CAN_HandleTypeDef* hcan)
{ if (get_can_ctx(hcan)) get_can_ctx(hcan)->RxFifo1FullCallbackCnt++; }

void HAL_CAN_SleepCallback(CAN_HandleTypeDef* hcan)
{ if (get_can_ctx(hcan)) get_can_ctx(hcan)->SleepCallbackCnt++; }

void HAL_CAN_WakeUpFromRxMsgCallback(CAN_HandleTypeDef* hcan)
{ if (get_can_ctx(hcan)) get_can_ctx(hcan)->WakeUpFromRxMsgCallbackCnt++; }

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* hcan)
{
    //__asm volatile ("bkpt");
    CAN_context* ctx = get_can_ctx(hcan);
    if (!ctx) return;
    volatile uint32_t original_error = hcan->ErrorCode;
    (void) original_error;

    // 处理 3 个 TX 邮箱的发送错误（主要为 NAK / Bus-Off / 付错重传超限）
    if (hcan->ErrorCode & HAL_CAN_ERROR_TX_ALST0)
    {
        SET_BIT(hcan->Instance->sTxMailBox[0].TIR, CAN_TI0R_TXRQ);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_ALST0;
    } else if (hcan->ErrorCode & HAL_CAN_ERROR_TX_TERR0)
    {
        tx_error(ctx, 0);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_EWG;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_ACK;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_TERR0;
    }

    if (hcan->ErrorCode & HAL_CAN_ERROR_TX_ALST1)
    {
        SET_BIT(hcan->Instance->sTxMailBox[1].TIR, CAN_TI1R_TXRQ);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_ALST1;
    } else if (hcan->ErrorCode & HAL_CAN_ERROR_TX_TERR1)
    {
        tx_error(ctx, 1);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_EWG;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_ACK;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_TERR1;
    }

    if (hcan->ErrorCode & HAL_CAN_ERROR_TX_ALST2)
    {
        SET_BIT(hcan->Instance->sTxMailBox[2].TIR, CAN_TI2R_TXRQ);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_ALST2;
    } else if (hcan->ErrorCode & HAL_CAN_ERROR_TX_TERR2)
    {
        tx_error(ctx, 2);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_EWG;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_ACK;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_TERR2;
    }

    if (hcan->ErrorCode)
        ctx->unexpected_errors++;
}

void CanSendMessage(CAN_context* canCtx, uint8_t* txData, CAN_TxHeaderTypeDef* txHeader)
{
    // 第三道保险：超时改为 100ms，避免任何遗漏的"未释放信号量"路径再次拖死
    // 调用线程。CAN 节点正常应答的典型耗时 < 1ms，100ms 已是极宽裕；超时即
    // 安静放弃本次发送，让上层重试或被 dceQueryDone=false 反映为离线。
    osStatus semaphore_status;
    if (canCtx->handle->Instance == CAN1)
        semaphore_status = osSemaphoreAcquire(sem_can1_tx, 100);
    else if (canCtx->handle->Instance == CAN2)
        semaphore_status = osSemaphoreAcquire(sem_can2_tx, 100);
    else
        return;

    if (semaphore_status == osOK)
        HAL_CAN_AddTxMessage(canCtx->handle, txHeader, txData, &canCtx->last_heartbeat_mailbox);
    // 拿不到就直接返回 —— 上层 osDelay + 回调超时已经能识别该节点没回包。
}

