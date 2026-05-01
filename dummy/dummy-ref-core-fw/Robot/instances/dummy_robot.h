#ifndef REF_STM32F4_FW_DUMMY_ROBOT_H
#define REF_STM32F4_FW_DUMMY_ROBOT_H

#include "algorithms/kinematic/6dof_kinematic.h"
#include "actuators/ctrl_step/ctrl_step.hpp"
#include "string"
#define ALL 0

/*
  |   PARAMS   | `current_limit` | `acceleration` | `dce_kp` | `dce_kv` | `dce_ki` | `dce_kd` |
  | ---------- | --------------- | -------------- | -------- | -------- | -------- | -------- |
  | **Joint1** | 2               | 30             | 1000     | 80       | 200      | 250      |
  | **Joint2** | 2               | 30             | 1000     | 80       | 200      | 200      |
  | **Joint3** | 2               | 30             | 1500     | 80       | 200      | 250      |
  | **Joint4** | 2               | 30             | 1000     | 80       | 200      | 250      |
  | **Joint5** | 2               | 30             | 1000     | 80       | 200      | 250      |
  | **Joint6** | 2               | 30             | 1000     | 80       | 200      | 250      |
 */


class StepHand : public CtrlStepMotor {
public:

    // 精简构造函数：只保留必要的CAN接口和电机ID参数
    StepHand(CAN_HandleTypeDef* hcan, uint8_t id)
            : CtrlStepMotor(hcan, id, false, 8, -115, 115) // 固定参数：inverse=false, reduction=1, 角度范围0-100
    {
    }

    float current=0.7;

    float OpenedAngle=-115;
    float ClosedAngle=115;

    //限速 控制位置
    void SetAngleWithSpeedLimit (float _angle)
    {
        float ag= ClosedAngle+_angle*2;
        SetAngleWithVelocityLimit(ag,70);
    }

    void SetPercentToAngle (float _percent)
    {
        float angle_=OpenedAngle+(_percent/100.0f)*230;


        SetAngleWithVelocityLimit(angle_,65);
    }
    //电流控制位置
    void SetAngleWithCurrentLimit(float inverse)
    {
        SetCurrentSetPoint(inverse*current);
    }

    //
    //校准位置模式
    void HandCalibration()
    {

        if (is_cali==1)
            return;
        is_cali=1;
        SetAngleWithVelocityLimit(OpenedAngle, 7.0f); //
        HAL_Delay(900);

        ApplyPositionAsHome();

        UpdateAngle();
        HAL_Delay(100);
        OpenedAngle= angle;

        SetAngleWithVelocityLimit(ClosedAngle, 7.0f); //
        HAL_Delay(900);

        UpdateAngle();
        HAL_Delay(100);
        ClosedAngle = angle;

        SetEnable(false);

        is_cali=0;
    }

    // 状态查询
    bool isEnabled() const {
        return state != STOP;
    }

private:
    bool is_cali=0;
};




class DummyRobot
{
public:
    explicit DummyRobot(CAN_HandleTypeDef* _hcan);
    ~DummyRobot();


    enum CommandMode
    {
        COMMAND_TARGET_POINT_SEQUENTIAL = 1,
        COMMAND_TARGET_POINT_INTERRUPTABLE,
        COMMAND_CONTINUES_TRAJECTORY,
        COMMAND_MOTOR_TUNING
    };

    enum RobotState
    {
        STATE_NORMAL = 0,        // 正常运行
        STATE_EMERGENCY_STOP,    // 急停（全轴同步机械停止，保持使能，保留队列）
        STATE_PAUSED             // 暂停（全轴同步停止，保持使能，保留队列）
    };

    volatile RobotState robotState = STATE_NORMAL;


    class TuningHelper
    {
    public:
        explicit TuningHelper(DummyRobot* _context) : context(_context)
        {
        }

        void SetTuningFlag(uint8_t _flag);
        void Tick(uint32_t _timeMillis);
        void SetFreqAndAmp(float _freq, float _amp);


        // Communication protocol definitions
        auto MakeProtocolDefinitions()
        {
            return make_protocol_member_list(
                make_protocol_function("set_tuning_freq_amp", *this,
                                       &TuningHelper::SetFreqAndAmp, "freq", "amp"),
                make_protocol_function("set_tuning_flag", *this,
                                       &TuningHelper::SetTuningFlag, "flag")
            );
        }


    private:
        DummyRobot* context;
        float time = 0;
        uint8_t tuningFlag = 0;
        float frequency = 1;
        float amplitude = 1;
    };
    TuningHelper tuningHelper = TuningHelper(this);


    // This is the pose when power on.
    const DOF6Kinematic::Joint6D_t REST_POSE = {0, -75, 180, 0, 0, 0};
//    const float DEFAULT_JOINT_SPEED = 30;  // degree/s
    const float DEFAULT_JOINT_SPEED = 45;  // degree/s

    const DOF6Kinematic::Joint6D_t DEFAULT_JOINT_ACCELERATION_BASES = {150, 100, 100, 200, 200, 100};
//    const float DEFAULT_JOINT_ACCELERATION_LOW = 30;    // 0~100
//    const float DEFAULT_JOINT_ACCELERATION_LOW = 15;    // 0~100
    float DEFAULT_JOINT_ACCELERATION_LOW = 10;    // 0~100
    const float DEFAULT_JOINT_ACCELERATION_HIGH = 65;  // 0~100
    const CommandMode DEFAULT_COMMAND_MODE = COMMAND_TARGET_POINT_INTERRUPTABLE;


    DOF6Kinematic::Joint6D_t currentJoints = REST_POSE;
    DOF6Kinematic::Joint6D_t targetJoints = REST_POSE;
    DOF6Kinematic::Joint6D_t initPose = REST_POSE;

    // 急停时保存被中断的内联运动目标（如 !HOME / !RESET），供 RESUME 续跑
    DOF6Kinematic::Joint6D_t pendingResumeTarget = REST_POSE;
    float pendingResumeSpeed = 0;            // 内联运动（HOME/REST）被中断时的速度，供 RESUME 续跑 MoveJ
    float stoppedJointSpeed = 0;             // 急停/暂停时刻冻结的 jointSpeed，RESUME 后还原
    volatile bool hasPendingResumeTarget = false;
    DOF6Kinematic::Pose6D_t currentPose6D = {};
    volatile uint8_t jointsStateFlag = 0b00000000;
    CommandMode commandMode = DEFAULT_COMMAND_MODE;
    CtrlStepMotor* motorJ[7] = {nullptr};
//    CtrlStepMotor* hand2 = {nullptr};
    StepHand* hand2 = {nullptr};


    void Init();
    bool MoveJ(float _j1, float _j2, float _j3, float _j4, float _j5, float _j6);
    bool MoveL(float _x, float _y, float _z, float _a, float _b, float _c);
    void MoveJoints(DOF6Kinematic::Joint6D_t _joints);
    void SetJointSpeed(float _speed);
    void SetJointAcceleration(float _acc);
    void UpdateJointAngles();
    void UpdateJointAnglesCallback();
    void UpdateJointPose6D();
    void Reboot();
    void SetEnable(bool _enable);
    void SetRGBEnable(bool _enable);
    bool GetRGBEnabled();
    void SetRGBMode(uint32_t mode);
    uint32_t GetRGBMode();
    void CalibrateHomeOffset();
    void Homing();
    void Resting();
    // !START：使能机械臂 + 使能手爪 + 回零点姿态（REST_POSE）。会重置 isHomed=false。
    void Startup();
    // !CLOSE：先回零点姿态 → 然后失能机械臂+手爪。会重置 isHomed=false。
    void Shutdown();
    // 手爪动作的 RGB 联动 hook（预留，未实现具体动效）。open=true 表示开手爪。
    void OnHandActionRGBHook(bool _open);
    bool IsMoving();
    bool IsEnabled();
    bool IsHomed() const { return isHomed; }
    float GetJointSpeed() const { return jointSpeed; }
    void SetCommandMode(uint32_t _mode);


    // Communication protocol definitions
    auto MakeProtocolDefinitions()
    {
        return make_protocol_member_list(
            make_protocol_function("calibrate_home_offset", *this, &DummyRobot::CalibrateHomeOffset),
            make_protocol_function("homing", *this, &DummyRobot::Homing),
            make_protocol_function("resting", *this, &DummyRobot::Resting),
            make_protocol_object("joint_1", motorJ[1]->MakeProtocolDefinitions()),
            make_protocol_object("joint_2", motorJ[2]->MakeProtocolDefinitions()),
            make_protocol_object("joint_3", motorJ[3]->MakeProtocolDefinitions()),
            make_protocol_object("joint_4", motorJ[4]->MakeProtocolDefinitions()),
            make_protocol_object("joint_5", motorJ[5]->MakeProtocolDefinitions()),
            make_protocol_object("joint_6", motorJ[6]->MakeProtocolDefinitions()),
            make_protocol_object("joint_all", motorJ[ALL]->MakeProtocolDefinitions()),
            make_protocol_function("reboot", *this, &DummyRobot::Reboot),
            make_protocol_function("set_enable", *this, &DummyRobot::SetEnable, "enable"),
            make_protocol_function("set_rgb_enable", *this, &DummyRobot::SetRGBEnable, "enable"),
            make_protocol_function("set_rgb_mode", *this, &DummyRobot::SetRGBMode, "mode"),
            make_protocol_function("move_j", *this, &DummyRobot::MoveJ, "j1", "j2", "j3", "j4", "j5", "j6"),
            make_protocol_function("move_l", *this, &DummyRobot::MoveL, "x", "y", "z", "a", "b", "c"),
            make_protocol_function("set_joint_speed", *this, &DummyRobot::SetJointSpeed, "speed"),
            make_protocol_function("set_joint_acc", *this, &DummyRobot::SetJointAcceleration, "acc"),
            make_protocol_function("set_command_mode", *this, &DummyRobot::SetCommandMode, "mode"),
            make_protocol_object("tuning", tuningHelper.MakeProtocolDefinitions())
        );
    }


    class CommandHandler
    {
    public:
        explicit CommandHandler(DummyRobot* _context) : context(_context)
        {
            commandFifo = osMessageQueueNew(16, 64, nullptr);
        }

        uint32_t Push(const std::string &_cmd);
        std::string Pop(uint32_t timeout);
        uint32_t ParseCommand(const std::string &_cmd);
        uint32_t GetSpace();
        void ClearFifo();
        void EmergencyStop();
        void Pause();
        void Resume();


    private:
        DummyRobot* context;
        osMessageQueueId_t commandFifo;
        char strBuffer[64]{};
    };
    CommandHandler commandHandler = CommandHandler(this);


private:
    CAN_HandleTypeDef* hcan;
    float jointSpeed = DEFAULT_JOINT_SPEED;
    float jointSpeedRatio = 1;
//    DOF6Kinematic::Joint6D_t dynamicJointSpeeds = {1, 1, 1, 1, 1, 1};
    DOF6Kinematic::Joint6D_t dynamicJointSpeeds = {0.5, 0.3, 0.2, 1, 1, 1};
    DOF6Kinematic* dof6Solver;
    bool isEnabled = false;
    // 是否已执行过 !HOME 校位。!START / SetEnable(false) 会复位为 false。
    // 任何会产生运动的用户命令在未 home 时被拒绝（见 ParseCommand 与 Push）。
    volatile bool isHomed = false;
    bool isRGBEnabled = true;
    uint32_t rgbMode = 3;
};


#endif //REF_STM32F4_FW_DUMMY_ROBOT_H
