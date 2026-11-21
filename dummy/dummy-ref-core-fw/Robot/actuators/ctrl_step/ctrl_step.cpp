#include "ctrl_step.hpp"
#include "communication.hpp"
#include <cstring>
#include <cmath>

CtrlStepMotor::CtrlStepMotor(CAN_HandleTypeDef* bus, uint8_t id, bool inverse,
                             float ratio, float minimum, float maximum) :
    nodeID(id), angleLimitMax(maximum), angleLimitMin(minimum),
    inverseDirection(inverse), reduction(ratio), hcan(bus) {}

// 每次发送使用栈上报文，避免控制、查询和急停线程覆盖同一个缓冲。
bool CtrlStepMotor::Send(uint8_t command, const void* value, uint8_t save)
{
    uint8_t data[8] = {};
    if (value) std::memcpy(data, value, 4);
    data[4] = save;
    CAN_TxHeaderTypeDef header = {};
    header.StdId = (nodeID << 7) | command;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = 8;
    return CanSendMessage(get_can_ctx(hcan), data, &header);
}

bool CtrlStepMotor::SetEnable(bool enabled)
{
    uint32_t value = enabled ? 1 : 0;
    if (!Send(0x01, &value)) return false;
    state = enabled ? FINISH : STOP;
    return true;
}
void CtrlStepMotor::SetEnableTemp(bool enabled) { uint32_t v = enabled; Send(0x7d, &v); }
void CtrlStepMotor::DoCalibration() { Send(0x02); }
void CtrlStepMotor::SetCurrentSetPoint(float v) { state = RUNNING; Send(0x03, &v); }
void CtrlStepMotor::SetVelocitySetPoint(float v) { state = RUNNING; Send(0x04, &v); }
void CtrlStepMotor::SetPositionSetPoint(float v) { Send(0x05, &v, 1); }
bool CtrlStepMotor::SetPositionWithVelocityLimit(float position, float velocity)
{
    uint8_t data[8];
    std::memcpy(data, &position, 4);
    std::memcpy(data + 4, &velocity, 4);
    CAN_TxHeaderTypeDef header = {};
    header.StdId = (nodeID << 7) | 0x07;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = 8;
    return CanSendMessage(get_can_ctx(hcan), data, &header);
}
void CtrlStepMotor::SetNodeID(uint32_t v) { Send(0x11, &v, 1); }
bool CtrlStepMotor::SetCurrentLimit(float v) { return Send(0x12, &v, 1); }
bool CtrlStepMotor::SetVelocityLimit(float v) { return Send(0x13, &v, 1); }
bool CtrlStepMotor::SaveAcceleration(float v) { return Send(0x14, &v, 1); }
bool CtrlStepMotor::SetLimit(uint8_t parameter, float value)
{
    if (parameter == 0) return SaveAcceleration(value);
    if (parameter == 1) return SetVelocityLimit(value);
    return parameter == 2 && SetCurrentLimit(value);
}
bool CtrlStepMotor::QueryLimit(uint8_t parameter)
{
    if (parameter > 2) return false;
    limitReceivedMask &= ~(1u << parameter);
    return Send(0x2c + parameter);
}
void CtrlStepMotor::LimitCallback(uint8_t parameter, float configured, float applied)
{
    uint32_t first, second;
    std::memcpy(&first, &configured, 4); std::memcpy(&second, &applied, 4);
    if (parameter > 2 || (first & 0x7f800000u) == 0x7f800000u ||
        (second & 0x7f800000u) == 0x7f800000u || configured < 0 || applied < 0) return;
    limits[parameter] = configured;
    appliedLimits[parameter] = applied;
    limitReceivedMask |= 1u << parameter;
    OnlineCallback();
}
void CtrlStepMotor::SetAcceleration(float v) { Send(0x14, &v); }
bool CtrlStepMotor::ApplyPositionAsHome() { return Send(0x15); }
void CtrlStepMotor::SetEnableOnBoot(bool enabled) { uint32_t v = enabled; Send(0x16, &v, 1); }
void CtrlStepMotor::SetDceKp(int32_t v) { Send(0x17, &v, 1); }
void CtrlStepMotor::SetDceKv(int32_t v) { Send(0x18, &v, 1); }
void CtrlStepMotor::SetDceKi(int32_t v) { Send(0x19, &v, 1); }
void CtrlStepMotor::SetDceKd(int32_t v) { Send(0x1a, &v, 1); }
void CtrlStepMotor::SetEnableStallProtect(bool enabled) { uint32_t v = enabled; Send(0x1b, &v, 1); }
void CtrlStepMotor::Reboot() { Send(0x7f); }
uint32_t CtrlStepMotor::GetTemp() { Send(0x25); return temperature; }
void CtrlStepMotor::EraseConfigs() { Send(0x7e); }

void CtrlStepMotor::SetAngle(float value)
{
    SetPositionSetPoint((inverseDirection ? -value : value) / 360.0f * reduction);
}
bool CtrlStepMotor::SetAngleWithVelocityLimit(float value, float velocity)
{
    return SetPositionWithVelocityLimit((inverseDirection ? -value : value) / 360.0f * reduction, velocity);
}
void CtrlStepMotor::UpdateAngle() { Send(0x23); }
void CtrlStepMotor::UpdateAngleCallback(float position, bool finished, bool stalled)
{
    if (!std::isfinite(position)) return;
    angle = position / reduction * 360.0f * (inverseDirection ? -1 : 1);
    state = finished ? FINISH : RUNNING;
    isStalled = stalled;
    lastAngleTick = HAL_GetTick();
    ++angleSequence;
}

void CtrlStepMotor::QueryDceKp() { dceReceivedMask &= ~1u; dceQueryDone = false; Send(0x27); }
void CtrlStepMotor::QueryDceKv() { dceReceivedMask &= ~2u; dceQueryDone = false; Send(0x28); }
void CtrlStepMotor::QueryDceKi() { dceReceivedMask &= ~4u; dceQueryDone = false; Send(0x29); }
void CtrlStepMotor::QueryDceKd() { dceReceivedMask &= ~8u; dceQueryDone = false; Send(0x2a); }
void CtrlStepMotor::QueryDceAll()
{
    dceReceivedMask = 0;
    dceQueryDone = false;
    Send(0x27);
    Send(0x28);
    Send(0x29);
    Send(0x2a);
}
void CtrlStepMotor::DceParamCallback(uint8_t parameter, int32_t value)
{
    switch (parameter) {
        case 0x27: dceKp = value; dceReceivedMask |= 1; break;
        case 0x28: dceKv = value; dceReceivedMask |= 2; break;
        case 0x29: dceKi = value; dceReceivedMask |= 4; break;
        case 0x2a: dceKd = value; dceReceivedMask |= 8; break;
        default: return;
    }
    dceQueryDone = true;
    OnlineCallback();
}
void CtrlStepMotor::QueryConfigSaved()
{
    configQueryDone = false;
    Send(0x2b);
}
void CtrlStepMotor::ConfigSavedCallback(bool saved)
{
    configSaved = saved;
    configQueryDone = true;
    OnlineCallback();
}
void CtrlStepMotor::QueryOnline() { isOnline = false; UpdateAngle(); }
bool CtrlStepMotor::QueryOnlineChecked()
{
    isOnline = false;
    return Send(0x23);
}
void CtrlStepMotor::OnlineCallback() { isOnline = true; lastResponseTick = HAL_GetTick(); }
