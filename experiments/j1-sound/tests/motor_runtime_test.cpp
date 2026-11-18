#include "configurations.h"
#include "Motor/motor.h"
#include <cassert>
#include <cstdio>

TestBoardConfig boardConfig;

class EncoderProbe : public EncoderBase
{
public:
    bool calibrated = true;
    bool Init() override { return true; }
    uint16_t UpdateAngle() override { return angleData.rectifiedAngle; }
    bool IsCalibrated() override { return calibrated; }
};

class DriverProbe : public DriverBase
{
public:
    unsigned excitationWrites = 0;
    void Init() override {}
    void Sleep() override {}
    void Brake() override {}
    void SetFocCurrentVector(uint32_t, int32_t) override {}
    void SetExcitationVector(uint32_t, int32_t, int32_t value, int32_t) override
    {
        assert(value >= 0 && value <= 100);
        ++excitationWrites;
    }
    void SetTwoCoilsCurrent(uint16_t, uint16_t) override {}
};

int main()
{
    EncoderProbe encoder;
    DriverProbe driver;
    Motor motor;
    motor.AttachEncoder(&encoder);
    motor.AttachDriver(&driver);
    boardConfig.enableMotorOnBoot = true;
    motor.controller->Init();
    assert(motor.controller->requestMode == Motor::MODE_STOP);
    for (int i = 0; i < 10; ++i) motor.Tick20kHz();
    assert(motor.controller->StartSound(1000, 200, 100) == 4);
    encoder.calibrated = false;
    assert(motor.controller->PrepareSoundHold() == 4);
    encoder.calibrated = true;
    // 当前编码器不在零点时，准备动作也不能把目标拉回零点。
    encoder.angleData.rectifiedAngle = 1024;
    for (int i = 0; i < 2000; ++i) motor.Tick20kHz();
    const auto position = motor.controller->GetPosition();
    assert(position != 0);
    assert(motor.controller->PrepareSoundHold() == 0);
    assert(!motor.controller->SoundHoldReady());
    for (int i = 0; i < 100; ++i) motor.Tick20kHz();
    assert(motor.controller->SoundHoldReady());
    assert(motor.controller->GetPosition() == position);
    assert(motor.controller->PrepareSoundHold() == 0);
    motor.controller->SetCtrlMode(Motor::MODE_COMMAND_POSITION);
    for (int i = 0; i < 10; ++i) motor.Tick20kHz();
    assert(motor.controller->StartSound(1000, 200, 100) == 0);
    for (int i = 0; i < 4010; ++i) motor.Tick20kHz();
    assert(driver.excitationWrites > 1000);
    assert(motor.controller->soundPulse.state == SoundPulse::FINISHED);
    assert(motor.controller->modeRunning == Motor::MODE_COMMAND_POSITION);
    assert(motor.controller->GetPosition() == position);
    for (int i = 0; i < 20000; ++i) motor.Tick20kHz();
    assert(motor.controller->StartSound(1000, 200, 100) == 0);
    for (int i = 0; i < 100; ++i) motor.Tick20kHz();
    const auto writes = driver.excitationWrites;
    // 模拟 STOP 实际使用的速度零模式；下一控制周期必须取消励磁。
    motor.controller->SetCtrlMode(Motor::MODE_COMMAND_VELOCITY);
    motor.controller->SetVelocitySetPoint(0);
    motor.Tick20kHz();
    assert(driver.excitationWrites == writes);
    assert(motor.controller->soundPulse.state == SoundPulse::ABORTED);
    assert(motor.controller->StartSound(1000, 200, 100) == 4);
    std::puts("真实电机控制器：未使能拒绝、定时完成、位置模式保持及速度零取消检查通过");
}
