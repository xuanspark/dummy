// ============================================================================
//  ctrl_step.hpp —— CAN 步进电机节点抽象
// ----------------------------------------------------------------------------
//  - 定义 ref 主控侧访问 35/42 电机子板的高层接口
//  - 统一角度单位、减速比、方向反转和软限位边界
//  - 暴露 Fibre 调试接口，便于上位机实时读取和下发参数
// ============================================================================

#ifndef DUMMY_CORE_FW_CTRL_STEP_HPP
#define DUMMY_CORE_FW_CTRL_STEP_HPP

#include "fibre/protocol.hpp"
#include "can.h"

class CtrlStepMotor
{
public:
    enum State
    {
        RUNNING,
        FINISH,
        STOP
    };


    const uint32_t CTRL_CIRCLE_COUNT = 200 * 256;

    CtrlStepMotor(CAN_HandleTypeDef* _hcan, uint8_t _id, bool _inverse = false, float _reduction = 1,
                  float _angleLimitMin = -180, float _angleLimitMax = 180);

    uint8_t nodeID;
    float angle = 0;
    float angleLimitMax;
    float angleLimitMin;
    uint32_t temperature = 0.0;
    bool inverseDirection;
    uint8_t reduction;
    State state = STOP;
    // 子板最近一次回调上报的堵转标志（35hand 在 0x23 回包 byte[5] 填，老 35motor/42motor 该位为 0）
    volatile bool isStalled = false;

    void SetAngle(float _angle);
    void SetAngleWithVelocityLimit(float _angle, float _vel);
    // CAN Command
    bool SetEnable(bool _enable);
    void SetEnableTemp(bool _enable);
    void DoCalibration();
    void SetCurrentSetPoint(float _val);
    void SetVelocitySetPoint(float _val);
    void SetPositionSetPoint(float _val);
    void SetPositionWithVelocityLimit(float _pos, float _vel);
    void SetNodeID(uint32_t _id);
    void SetCurrentLimit(float _val);
    void SetVelocityLimit(float _val);
    void SetAcceleration(float _val);
    void SetDceKp(int32_t _val);
    void SetDceKv(int32_t _val);
    void SetDceKi(int32_t _val);
    void SetDceKd(int32_t _val);
    // 实时 CAN 查询 DCE 参数（发送请求，由回调更新 dceXxx）
    void QueryDceKp();
    void QueryDceKv();
    void QueryDceKi();
    void QueryDceKd();
    void QueryDceAll();   // 一次发4个请求
    // 回调（由 can_protocol.cpp 调用）
    void DceParamCallback(uint8_t paramId, int32_t value);
    // 查询结果（回调更新，外部读取）
    volatile int32_t dceKp = 0;
    volatile int32_t dceKv = 0;
    volatile int32_t dceKi = 0;
    volatile int32_t dceKd = 0;
    volatile bool    dceQueryDone = false;  // 标记本轮查询是否有回调
    volatile uint8_t dceReceivedMask = 0;
    volatile uint32_t lastAngleTick = 0;
    volatile uint32_t angleSequence = 0;
    volatile bool configQueryDone = false;
    volatile bool configSaved = false;
    void QueryConfigSaved();
    void ConfigSavedCallback(bool saved);

    // ---- 在线检测 ----
    void QueryOnline();                     // 发 CAN 探测帧
    void OnlineCallback();                  // 回调标记在线
    volatile bool isOnline = false;         // 当前在线状态
    volatile uint32_t lastResponseTick = 0; // 最近一次回调的 tick

    bool ApplyPositionAsHome();
#ifdef J1_SOUND_EXPERIMENT
    bool SendSoundTest(uint8_t operation, uint8_t token, uint16_t frequency = 0,
                       uint16_t duration = 0, uint16_t amplitude = 0);
    volatile uint8_t soundAckToken = 0, soundResult = 0, soundState = 0;
    volatile uint8_t soundCapability = 0, soundHoldActive = 0, soundHoldReady = 0;
    volatile uint8_t soundDiagnosticToken = 0, soundWave = 0;
    volatile uint16_t soundInjectedTicks = 0, soundInjectedPeak = 0;
#endif
    void SetEnableOnBoot(bool _enable);
    void SetEnableStallProtect(bool _enable);
    void Reboot();
    uint32_t GetTemp();
    void EraseConfigs();

    void UpdateAngle();
    void UpdateAngleCallback(float _pos, bool _isFinished, bool _isStalled = false);


    // Communication protocol definitions
    auto MakeProtocolDefinitions()
    {
        return make_protocol_member_list(
            make_protocol_ro_property("angle", &angle),
            make_protocol_function("get_temperature", *this, &CtrlStepMotor::GetTemp),
            make_protocol_function("update_angle", *this, &CtrlStepMotor::UpdateAngle)
        );
    }


private:
    CAN_HandleTypeDef* hcan;
    bool Send(uint8_t command, const void* value = nullptr, uint8_t save = 0);
};

#endif //DUMMY_CORE_FW_CTRL_STEP_HPP
