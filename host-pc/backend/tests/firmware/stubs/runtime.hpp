#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

using osMessageQueueId_t = void*;
using osThreadId_t = void*;
using osPriority_t = int;
using osStatus_t = int;
constexpr int osOK = 0;
constexpr uint32_t osWaitForever = 0xffffffff;
constexpr int osPriorityNormal = 0;
constexpr int osPriorityRealtime7 = 55;
constexpr int osFlagsWaitAny = 0;
struct osThreadAttr_t { const char* name; uint32_t stack_size; osPriority_t priority; };
osMessageQueueId_t osMessageQueueNew(uint32_t, uint32_t, const void*);
int osMessageQueuePut(void*, const void*, uint8_t, uint32_t);
int osMessageQueueGet(void*, void*, uint8_t*, uint32_t);
uint32_t osMessageQueueGetSpace(void*);
void osMessageQueueReset(void*);
void* osThreadNew(void (*)(void*), void*, const osThreadAttr_t*);
uint32_t osThreadFlagsSet(void*, uint32_t);
uint32_t osThreadFlagsWait(uint32_t, uint32_t, uint32_t);
void osDelay(uint32_t);
uint32_t HAL_GetTick();
inline void HAL_Delay(uint32_t value) { osDelay(value); }
inline uint32_t simulatedPrimask = 0;
inline uint32_t __get_PRIMASK() { return simulatedPrimask; }
inline void __disable_irq() { simulatedPrimask = 1; }
inline void __set_PRIMASK(uint32_t value) { simulatedPrimask = value; }
void HAL_NVIC_SystemReset();

struct CAN_HandleTypeDef { int Instance = 1; };
struct CAN_TxHeaderTypeDef {
    uint32_t StdId = 0, ExtId = 0, IDE = 0, RTR = 0, DLC = 0, TransmitGlobalTime = 0;
};
struct CAN_context { CAN_HandleTypeDef* handle; };
constexpr int CAN_ID_STD = 0;
constexpr int CAN_RTR_DATA = 0;
struct TIM_HandleTypeDef {};
extern TIM_HandleTypeDef htim2;
constexpr int TIM_CHANNEL_4 = 4;
using HAL_StatusTypeDef = int;
constexpr int HAL_OK = 0;
inline int HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef*, int, uint32_t*, uint32_t) { return 1; }
inline int HAL_TIM_PWM_Stop_DMA(TIM_HandleTypeDef*, int) { return 0; }

class StreamSink {
public:
    enum { CHANNEL_TYPE_USB, CHANNEL_TYPE_UART4, CHANNEL_TYPE_UART5 };
    uint8_t channelType = CHANNEL_TYPE_USB;
    virtual ~StreamSink() = default;
    virtual int process_bytes(const uint8_t*, size_t, size_t*) = 0;
    virtual size_t get_free_space() = 0;
};
template<typename... Args> int make_protocol_member_list(Args&&...) { return 0; }
template<typename... Args> int make_protocol_function(Args&&...) { return 0; }
template<typename... Args> int make_protocol_object(Args&&...) { return 0; }
template<typename... Args> int make_protocol_ro_property(Args&&...) { return 0; }
extern StreamSink* usbStreamOutputPtr;
extern StreamSink* uart4StreamOutputPtr;
extern StreamSink* uart5StreamOutputPtr;
CAN_context* get_can_ctx(CAN_HandleTypeDef*);
bool CanSendMessage(CAN_context*, uint8_t*, CAN_TxHeaderTypeDef*);
