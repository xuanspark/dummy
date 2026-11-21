#include "common_inc.h"
#include "ascii_processor.hpp"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include "interface_usb.hpp"
#include <cstring>

extern DummyRobot dummy;

osThreadId_t usbServerTaskHandle;
USBStats_t usb_stats_ = {0};

// 在 USB ISR 上下文扫描数据包中是否包含指定报文
static EmergencyStopScanner usbStopScanner;

class USBSender : public PacketSink
{
public:
    USBSender(uint8_t endpoint_pair, const osSemaphoreId &sem_usb_tx)
        : endpoint_pair_(endpoint_pair), sem_usb_tx_(sem_usb_tx)
    {}

    int process_packet(const uint8_t *buffer, size_t length) override
    {
        // 不能发部分包（USB CDC 以包为单位）
        if (length > USB_TX_DATA_SIZE)
            return -1;
        // 等 USB IN 端点空闲。原版用 PROTOCOL_SERVER_TIMEOUT_MS=10ms：单次 IN
        // 已是 1ms 量级，但 ASCII 命令一行行连续 Respond 时容易 race（前一行
        // 的 DataIn 回调还没跑就尝试发下一行）。100ms 足够覆盖 OS 调度抖动。
        if (osSemaphoreAcquire(sem_usb_tx_, 100) != osOK)
        {
            // 拿不到信号量说明 TX-complete 中断长时间没来（USB 被 host reset、
            // 拔线、driver 卡住等）。原版会**继续强行 CDC_Transmit_FS**，但此时
            // hEP_Tx->State==1 必然让 USBD_CDC_TransmitPacket 返回 USBD_BUSY，
            // 数据被丢，更糟糕的是把 sem_usb_tx 错误地 release 掉，让计数失衡。
            // 现在改为：直接放弃这一包（计数 +1，让上层感知），返回 -1，
            // 让 Respond 的调用方至少不再产生级联错误。
            usb_stats_.tx_overrun_cnt++;
            return -1;
        }

        // 发送包
        std::memcpy(tx_buffer_, buffer, length);
        uint8_t status = CDC_Transmit_FS(tx_buffer_, length, endpoint_pair_);
        if (status != USBD_OK)
        {
            osSemaphoreRelease(sem_usb_tx_);
            return -1;
        }
        usb_stats_.tx_cnt++;

        return 0;
    }

private:
    uint8_t endpoint_pair_;
    const osSemaphoreId &sem_usb_tx_;
    uint8_t tx_buffer_[USB_TX_DATA_SIZE];
};

// 两个发送器使用各自的信号量，避免 CDC 与 Native 的完成回调互相影响。
USBSender usb_packet_output_cdc(CDC_OUT_EP, sem_usb_tx_cdc);
USBSender usb_packet_output_native(ODRIVE_OUT_EP, sem_usb_tx_native);

class TreatPacketSinkAsStreamSink : public StreamSink
{
public:
    TreatPacketSinkAsStreamSink(PacketSink &output) : output_(output)
    {
        channelType = CHANNEL_TYPE_USB;
    }

    int process_bytes(const uint8_t *buffer, size_t length, size_t *processed_bytes)
    {
        // 循环以保证多个分包都能发出
        while (length)
        {
            size_t chunk = length < USB_TX_DATA_SIZE ? length : USB_TX_DATA_SIZE;
            if (output_.process_packet(buffer, chunk) != 0)
                return -1;
            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;
        }
        return 0;
    }

    size_t get_free_space()
    { return SIZE_MAX; }



private:
    PacketSink &output_;
} usb_stream_output(usb_packet_output_cdc);

// 被 printf 重定向机制使用。上面那些静态对象与下面看似随意的指针（被 extern 出去）准为此服务。
// TODO: 重构以减少肠面代码
StreamSink *usbStreamOutputPtr = &usb_stream_output;

BidirectionalPacketBasedChannel usb_channel(usb_packet_output_native);


struct USBInterface
{
    static constexpr uint8_t RX_QUEUE_DEPTH = 4;
    struct Packet {
        uint8_t data[USB_RX_DATA_SIZE];
        uint32_t len;
    };
    Packet rx_queue[RX_QUEUE_DEPTH];
    volatile uint8_t rx_head = 0;
    volatile uint8_t rx_tail = 0;
    volatile uint8_t rx_count = 0;
    volatile uint32_t rx_drop_cnt = 0;
    uint8_t out_ep;
    uint8_t in_ep;
    USBSender &usb_sender;
};

// 注意：静态对象让这里不够模块化。
// 注意：使用单一 RX 信号量 + 轮询 data_pending 使单一泵循环线程能处理两个接口。
static USBInterface CDC_interface = {
    .out_ep = CDC_OUT_EP,
    .in_ep = CDC_IN_EP,
    .usb_sender = usb_packet_output_cdc,
};
static USBInterface ODrive_interface = {
    .out_ep = ODRIVE_OUT_EP,
    .in_ep = ODRIVE_IN_EP,
    .usb_sender = usb_packet_output_native,
};

static void UsbServerTask(void *ctx)
{
    (void) ctx;

    for (;;)
    {
        // const uint32_t usb_check_timeout = 1; // 毫秒
        osStatus sem_stat = osSemaphoreAcquire(sem_usb_rx, osWaitForever);
        if (sem_stat == osOK)
        {
            for (USBInterface* iface : {&CDC_interface, &ODrive_interface})
            {
                for (;;)
                {
                    USBInterface::Packet packet{};
                    uint32_t mask = __get_PRIMASK();
                    __disable_irq();
                    if (iface->rx_count == 0)
                    {
                        __set_PRIMASK(mask);
                        break;
                    }
                    const uint8_t slot = iface->rx_tail;
                    packet.len = iface->rx_queue[slot].len;
                    std::memcpy(packet.data, iface->rx_queue[slot].data, packet.len);
                    iface->rx_tail = (slot + 1) % USBInterface::RX_QUEUE_DEPTH;
                    --iface->rx_count;
                    __set_PRIMASK(mask);

                    usb_stats_.rx_cnt++;
                    USBD_CDC_ReceivePacket(&hUsbDeviceFS, iface->out_ep);
                    if (iface == &CDC_interface)
                        ASCII_protocol_parse_stream(packet.data, packet.len, usb_stream_output);
                    else
                        usb_channel.process_packet(packet.data, packet.len);
                }
            }
        }
    }
}

// 由 CDC_Receive_FS 回调调用。只负责“记录 + 唤醒 UsbServerTask”，
// 以使该线程在 RTOS 上下文里处理该数据（避免中断中跳转重逻辑）。
void usb_rx_process_packet(uint8_t *buf, uint32_t len, uint8_t endpoint_pair)
{
    USBInterface *usb_iface;
    if (endpoint_pair == CDC_interface.out_ep)
    {
        usb_iface = &CDC_interface;
    } else if (endpoint_pair == ODrive_interface.out_ep)
    {
        usb_iface = &ODrive_interface;
    } else
    {
        return;
    }

    // 运行于 USB CDC 中断上下文：在在期交给 UsbServerTask 处理之前偷拍急停/暂停指令，
    // 只设置状态位（原子写），不作可能阻塞的调用。FixUpdate 会下一个 tick 看到
    // 状态变更并广播 SetVelocitySetPoint(0)，同时在在运行的 Homing()/Resting()
    // 的状态感知等待循环会立即跳出。
    // 仅 !STOP 需要硬中断式抢占（在 ISR 立即设置状态使 FixUpdate 在 5ms 内广播 V=0）。
    // !PAUSE 是软暂停语义，不在 ISR 处理——交给 OnUsbAsciiCmd 在当前动作完成后切状态。
    if (endpoint_pair == CDC_interface.out_ep && len > 0)
    {
        if (usbStopScanner.Feed(buf, len))
            dummy.RequestEmergencyStopFromISR();
    }

    if (len > USB_RX_DATA_SIZE) len = USB_RX_DATA_SIZE;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (usb_iface->rx_count >= USBInterface::RX_QUEUE_DEPTH)
    {
        ++usb_iface->rx_drop_cnt;
        ++usb_stats_.rx_drop_cnt;
        __set_PRIMASK(mask);
        return;
    }
    const uint8_t slot = usb_iface->rx_head;
    std::memcpy(usb_iface->rx_queue[slot].data, buf, len);
    usb_iface->rx_queue[slot].len = len;
    usb_iface->rx_head = (slot + 1) % USBInterface::RX_QUEUE_DEPTH;
    ++usb_iface->rx_count;
    __set_PRIMASK(mask);
    osSemaphoreRelease(sem_usb_rx);
}


const osThreadAttr_t usbServerTask_attributes = {
    .name = "UsbServerTask",
    .stack_size = 2000,
    .priority = (osPriority_t) osPriorityNormal,
};

bool StartUsbServer()
{
    // 启动 USB 服务线程
    usbServerTaskHandle = osThreadNew(UsbServerTask, nullptr, &usbServerTask_attributes);
    return usbServerTaskHandle != nullptr;
}
