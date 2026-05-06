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
static inline bool peek_token(const uint8_t* buf, size_t len, const char* tok)
{
    size_t tl = std::strlen(tok);
    if (len < tl) return false;
    for (size_t i = 0; i + tl <= len; i++)
    {
        if (std::memcmp(buf + i, tok, tl) == 0)
            return true;
    }
    return false;
}

class USBSender : public PacketSink
{
public:
    USBSender(uint8_t endpoint_pair, const osSemaphoreId &sem_usb_tx)
        : endpoint_pair_(endpoint_pair), sem_usb_tx_(sem_usb_tx)
    {}

    int process_packet(const uint8_t *buffer, size_t length) override
    {
        // cannot send partial packets
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

        // transmit packet
        uint8_t status = CDC_Transmit_FS(const_cast<uint8_t *>(buffer), length, endpoint_pair_);
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
};

// Note we could have independent semaphores here to allow concurrent transmission
USBSender usb_packet_output_cdc(CDC_OUT_EP, sem_usb_tx);
USBSender usb_packet_output_native(ODRIVE_OUT_EP, sem_usb_tx);

class TreatPacketSinkAsStreamSink : public StreamSink
{
public:
    TreatPacketSinkAsStreamSink(PacketSink &output) : output_(output)
    {
        channelType = CHANNEL_TYPE_USB;
    }

    int process_bytes(const uint8_t *buffer, size_t length, size_t *processed_bytes)
    {
        // Loop to ensure all bytes get sent
        while (length)
        {
            size_t chunk = length < USB_TX_DATA_SIZE ? length : USB_TX_DATA_SIZE;
            if (output_.process_packet(buffer, length) != 0)
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

// This is used by the printf feature. Hence the above statics, and below seemingly random ptr (it's externed)
// TODO: less spaghetti code
StreamSink *usbStreamOutputPtr = &usb_stream_output;

BidirectionalPacketBasedChannel usb_channel(usb_packet_output_native);


struct USBInterface
{
    uint8_t *rx_buf = nullptr;
    uint32_t rx_len = 0;
    bool data_pending = false;
    uint8_t out_ep;
    uint8_t in_ep;
    USBSender &usb_sender;
};

// Note: statics make this less modular.
// Note: we use a single rx semaphore and loop over data_pending to allow a single pump loop thread
static USBInterface CDC_interface = {
    .rx_buf = nullptr,
    .rx_len = 0,
    .data_pending = false,
    .out_ep = CDC_OUT_EP,
    .in_ep = CDC_IN_EP,
    .usb_sender = usb_packet_output_cdc,
};
static USBInterface ODrive_interface = {
    .rx_buf = nullptr,
    .rx_len = 0,
    .data_pending = false,
    .out_ep = ODRIVE_OUT_EP,
    .in_ep = ODRIVE_IN_EP,
    .usb_sender = usb_packet_output_native,
};

static void UsbServerTask(void *ctx)
{
    (void) ctx;

    for (;;)
    {
        // const uint32_t usb_check_timeout = 1; // ms
        osStatus sem_stat = osSemaphoreAcquire(sem_usb_rx, osWaitForever);
        if (sem_stat == osOK)
        {
            usb_stats_.rx_cnt++;

            // CDC Interface
            if (CDC_interface.data_pending)
            {
                CDC_interface.data_pending = false;

                // 快照输入数据，随即 USBD_CDC_ReceivePacket 以允许下一包在当前命令还未处理完时到达
                // （例如 !HOME 造成 Homing() 阻塞期间 !STOP 仍能被接收并被 ISR 偷拍）
                static uint8_t cdc_snapshot[USB_RX_DATA_SIZE];
                size_t snap_len = CDC_interface.rx_len;
                if (snap_len > sizeof(cdc_snapshot)) snap_len = sizeof(cdc_snapshot);
                std::memcpy(cdc_snapshot, CDC_interface.rx_buf, snap_len);

                USBD_CDC_ReceivePacket(&hUsbDeviceFS, CDC_interface.out_ep);  // Allow next packet EARLY

                ASCII_protocol_parse_stream(cdc_snapshot, snap_len, usb_stream_output);
            }

            // Native Interface
            if (ODrive_interface.data_pending)
            {
                ODrive_interface.data_pending = false;
                usb_channel.process_packet(ODrive_interface.rx_buf, ODrive_interface.rx_len);
                USBD_CDC_ReceivePacket(&hUsbDeviceFS, ODrive_interface.out_ep);  // Allow next packet
            }
        }
    }
}

// Called from CDC_Receive_FS callback function, this allows the communication
// thread to handle the incoming data
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
        if (peek_token(buf, len, "!STOP"))
            dummy.robotState = DummyRobot::STATE_EMERGENCY_STOP;
    }

    // We don't allow the next USB packet until the previous one has been processed completely.
    // Therefore it's safe to write to these vars directly since we know previous processing is complete.
    usb_iface->rx_buf = buf;
    usb_iface->rx_len = len;
    usb_iface->data_pending = true;
    osSemaphoreRelease(sem_usb_rx);
}


const osThreadAttr_t usbServerTask_attributes = {
    .name = "UsbServerTask",
    .stack_size = 2000,
    .priority = (osPriority_t) osPriorityNormal,
};

void StartUsbServer()
{
    // Start USB communication thread
    usbServerTaskHandle = osThreadNew(UsbServerTask, nullptr, &usbServerTask_attributes);
}
