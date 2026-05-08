// ============================================================================
//  communication.cpp —— 通信子系统初始化与任务实现
// ----------------------------------------------------------------------------
//  - InitCommunication 起 commTask 线程，把 USB / UART / CAN 三条物理通道串起来
//  - 处理 USB 端点列表的就绪标志（endpointListValid）
//  - 提供 CommunicationTask（异步轮询所有通道）与 UsbDeferredInterruptTask
//  - 物理层细节在 interface_usb / interface_uart / interface_can 中实现
// ============================================================================

/* ----- Includes -------------------------------------------------------- */

#include "communication.hpp"
#include "common_inc.h"

/* ----- 私有变量 ----------------------------------------------------- */
// fibre 对象树是否已 publish 完成。InitCommunication 会自旋等它变 true
// 后才返回，以保证上层 RGB / OLED / 运动等任务启动时下层已就绪。
volatile bool endpointListValid = false;

/* ----- 函数实现 ----------------------------------------------------- */
// commTask 句柄与属性：负责依次启动 UART / USB / CAN1 / CAN2 服务器。
// 需要 45 KB 栈是因为 fibre 发布时会临时拼出较大的对象树。
osThreadId_t commTaskHandle;
const osThreadAttr_t commTask_attributes = {
    .name = "commTask",
    .stack_size = 45000,
    .priority = (osPriority_t) osPriorityNormal,
};

// InitCommunication: 由 Main() 启动序列首先调。
// 1) 创建 commTask 线程（在里面 publish fibre + 启动各物理服务器）
// 2) 自旋等 endpointListValid=true，确保返回后下层调用均安全。
void InitCommunication(void)
{
    // Start command handling thread
    commTaskHandle = osThreadNew(CommunicationTask, nullptr, &commTask_attributes);

    // 阻塞等待 commTask 完成 fibre publish。未就绪前不允许返回，
    // 否则主初始化继续处理运动 / RGB 可能拿到未初始化的 endpoint。
    while (!endpointListValid)
        osDelay(1);
}

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
osThreadId_t usbIrqTaskHandle;

// UsbDeferredInterruptTask: USB 中断中转线程。
// USB OTG_FS 中断里只做一件事：释放 sem_usb_irq 信号量 + 禁下一次 IRQ。
// 实际调用 HAL_PCD_IRQHandler 这种耗时逻辑在本线程里跳，
// 这样避免 ISR 执行太久危及其他高优先级任务。
void UsbDeferredInterruptTask(void* ctx)
{
    (void) ctx; // unused parameter

    for (;;)
    {
        // 等一个者 USB 中断释放信号量（OTG_FS_IRQHandler）
        osStatus semaphore_status = osSemaphoreAcquire(sem_usb_irq, osWaitForever);
        if (semaphore_status == osOK)
        {
            // 由本线程接手处理本次 USB 事件
            HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
            // 处理完毕后重新使能 IRQ，下一次中断才能进入
            HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
        }
    }
}

// CommunicationTask: 初始化线程体。顺序重要：
//   1. CommitProtocol() —— 将顶层对象树 publish 给 fibre。这一步必须在
//      启动各物理服务器之前完成，否则远程调用会看不到 endpoint。
//   2. 置 endpointListValid=true 让 InitCommunication 返回。
//   3. 依次启动 UART / USB / CAN1 / CAN2 服务器。
//   4. 所有实际接受逻辑都在各自的子线程里跑，本线程仕事后不再需要东西，
//      必要的废弃 sleep 仅为让 RTOS 调度器交出控制。
void CommunicationTask(void* ctx)
{
    (void) ctx; // unused parameter

    CommitProtocol();

    // 让 InitCommunication 插点可以返回、主初始化继续
    endpointListValid = true;

    StartUartServer();
    StartUsbServer();
    StartCanServer(CAN1);
    StartCanServer(CAN2);

    for (;;)
    {
        osDelay(1000); // 本线程仅作为中转，不需要主动处理会话
    }
}

// newlib 的低层写接口。printf / puts / std::cout 最终都会调下面的 _write。
extern "C" {
int _write(int file, const char* data, int len);
}

// _write: printf 内部实际发送字节的函数。本项目中同时复制到 USB 与 UART4，
// 两个物理通道都能看到调试输出。注意 Respond() 是定向单通道，
// 与 printf 不同。
int _write(int file, const char* data, int len)
{
    usbStreamOutputPtr->process_bytes((const uint8_t*) data, len, nullptr);
    uart4StreamOutputPtr->process_bytes((const uint8_t*) data, len, nullptr);

    return len;
}
