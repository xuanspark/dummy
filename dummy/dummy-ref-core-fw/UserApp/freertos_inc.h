// ============================================================================
//  freertos_inc.h —— FreeRTOS / CMSIS-RTOS 头文件汇总
// ----------------------------------------------------------------------------
//  统一暴露 osMessageQueue、osThread、osSemaphore 等 RTOS API。
//  与 common_inc.h 配合使用。
// ============================================================================
#ifndef __FREERTOS_H
#define __FREERTOS_H

#ifdef __cplusplus
extern "C" {
#endif

// ----- 信号量清单（由 freertos.c 定义、上层访问） -----
extern osSemaphoreId sem_usb_irq;       // USB OTG_FS_IRQHandler 重载到中转线程的唤醒信号
extern osSemaphoreId sem_uart4_dma;     // UART4 TxCplt 后释放，让下一次 process_bytes 能发送
extern osSemaphoreId sem_uart5_dma;     // UART5 同上
extern osSemaphoreId sem_usb_rx;        // USB CDC RX 包准备就绪的信号
extern osSemaphoreId sem_usb_tx;        // USB CDC TX 完成信号（CDC_TransmitCplt 释放）
extern osSemaphoreId sem_can1_tx;       // CAN1 TX 邮箱空闲信号（TxComplete/Abort/Error 释放）
extern osSemaphoreId sem_can2_tx;       // CAN2 同上

// ----- 线程句柄清单 -----
/*--------------------------------- 系统线程 -------------------------------------*/
extern osThreadId_t defaultTaskHandle;      // CubeMX 默认任务、辊跳到本项目的 Main()。Usage: 2000 Bytes stack
extern osThreadId_t commTaskHandle;         // 启动三个服务器 + fibre publish。Usage: 48000 Bytes stack
extern osThreadId_t usbIrqTaskHandle;       // USB 中断中转线程。Usage: 512  Bytes stack
extern osThreadId_t usbServerTaskHandle;    // USB CDC 定期处理。Usage: 2048 Bytes stack
extern osThreadId_t uartServerTaskHandle;   // UART 轮询线程。Usage: 2048 Bytes stack

/*---------------------------------- 用户线程 --------------------------------------*/
extern osThreadId_t oledTaskHandle;              // OLED 刷帧 + IMU 采样。Usage: 4000 Bytes stack
extern osThreadId_t controlLoopFixUpdateHandle;  // 控制环主体（200 µs tick）。Usage: 4000 Bytes stack

/*---------------- 60K (已使用) / 64K（FreeRTOS 在 CCRAM 总限） ------------------*/


#ifdef __cplusplus
}
#endif

#endif // __FREERTOS_H —— 头文件包含守卫结束