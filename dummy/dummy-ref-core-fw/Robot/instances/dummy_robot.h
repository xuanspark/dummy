// ============================================================================
//  dummy_robot.h —— Dummy 六轴机器人对象定义
// ----------------------------------------------------------------------------
//  - 汇总关节电机、手爪、运动学、命令队列、RGB 自动模式等核心状态
//  - 定义 NORMAL / EMERGENCY_STOP / PAUSED 三态运行状态机
//  - 对外提供 MoveJ、MoveL、Homing、Resting 以及系统命令入口
// ============================================================================

#ifndef REF_STM32F4_FW_DUMMY_ROBOT_H
#define REF_STM32F4_FW_DUMMY_ROBOT_H

#include "algorithms/kinematic/6dof_kinematic.h"
#include "actuators/ctrl_step/ctrl_step.hpp"
#include "string"
#include "rgb.hpp"
#define ALL 0

// EEPROM 地址分配
#define EEPROM_ADDR_RGB_MODE   0x00   // 1 byte: rgbMode (0~11)

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

    // 手爪独立的使能标志（与基类 state 解耦：基类 state 会被位置回调覆盖成 FINISH/RUNNING）
    bool enabledFlag = false;
    void SetEnable(bool _enable)
    {
        enabledFlag = _enable;
        CtrlStepMotor::SetEnable(_enable);
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
        // 约定：_percent = 0 → 完全关闭，_percent = 100 → 完全张开
        // 公式自适应任意符号的 OpenedAngle / ClosedAngle（不依赖固定 230° 行程）
        float angle_ = ClosedAngle + (_percent / 100.0f) * (OpenedAngle - ClosedAngle);
        // 中点以下视为偏向闭合（用于 GetState 三态判别）
        lastCloseRequested = (_percent < 50.0f);
        // 速度限制 10 r/s：手爪 dce_kp=195 偏低、负载较大，过快会让位置环跟不上软轨迹
        // 触发 300 ms 估计速度阈值堵转保护，电机被 driver->Sleep() 切断，看起来"几乎不动 + LED 闪烁"。
        SetAngleWithVelocityLimit(angle_, 10);
    }
    //电流控制位置：inverse > 0 = 闭合（+电流），inverse < 0 = 张开（-电流）
    void SetAngleWithCurrentLimit(float inverse)
    {
        lastCloseRequested = (inverse > 0);
        SetCurrentSetPoint(inverse*current);
    }

    //
    // 校准：用电流模式顶到机械限位，再读编码器 → 不依赖任何旧的 OpenedAngle/ClosedAngle 值，
    // 即使 EEPROM 零点和主控 OpenedAngle/ClosedAngle 都被污染过也能恢复。
    //
    // 流程：先开 → 再合（合处归零，ClosedAngle = 0）→ 再开（读 OpenedAngle 数值）
    // 这样输出 ClosedAngle = 0、OpenedAngle = 行程数值，视觉上"先开再合"（第三步只是量值）。
    void HandCalibration()
    {
        if (is_cali == 1)
            return;
        is_cali = 1;

        // ---- Step 1: 张开到限位（不归零，仅为视觉上的"先开"）----
        SetAngleWithCurrentLimit(-1);   // -1 = 张开（与 !HD_O 一致）
        HAL_Delay(1500);

        // ---- Step 2: 闭合到限位 ----
        SetAngleWithCurrentLimit(+1);   // +1 = 闭合（与 !HD_C 一致）
        HAL_Delay(1500);

        // ---- Step 3: 在闭合限位处归零 → 闭合位置就是 0 ----
        ApplyPositionAsHome();
        HAL_Delay(150);                 // 等 EEPROM commit + slave 主循环刷新

        UpdateAngle();
        HAL_Delay(100);
        ClosedAngle = angle;            // 闭合 ≈ 0（刚归零）

        // ---- Step 4: 再次张开到限位，读取相对零点的数值 ----
        SetAngleWithCurrentLimit(-1);
        HAL_Delay(1500);

        UpdateAngle();
        HAL_Delay(100);
        OpenedAngle = angle;            // 张开位置（相对闭合零点，可能为负，看安装方向）

        // ---- Step 5: 末尾合一下，让手爪停在闭合位置（用户期望的静止状态）----
        SetAngleWithCurrentLimit(+1);
        HAL_Delay(1500);

        // ---- Step 6: 释放电流，避免持续夹紧产热 ----
        SetCurrentSetPoint(0);
        HAL_Delay(50);
        SetEnable(false);

        // 校准结束时手爪物理位于闭合位置，标志同步以避免下次 #GETJPOS 误报为张开
        lastCloseRequested = true;

        is_cali = 0;
    }

    // 状态查询：使用独立标志，避免被 UpdateAngleCallback 改写的 state 字段干扰
    bool isEnabled() const {
        return enabledFlag;
    }

    // 三态手爪状态（与 ASCII 协议 H 字段一致）
    enum HandState { HS_CLOSED = 0, HS_OPEN = 1, HS_GRIPPED = 2 };

    // 综合判别手爪当前状态。决策树（按优先级从高到低）：
    //   1. 失能：相信最后一次命令的方向语义（电机不作用力，isStalled 可能是 sticky 残留）
    //   2. 堵转 + 关爪命令：偏离闭合零点 epsilon 以上 → 中段堵转 = 夹住物体 (HS_GRIPPED)
    //                       否则 → 已撞合拢端机械限位 (HS_CLOSED)
    //   3. 堵转 + 张爪命令：撞张开端 (HS_OPEN)
    //   4. 未堵转：以最后一次命令的方向语义为准
    HandState GetState(float epsilon = 5.0f) const {
        if (!enabledFlag)
            return lastCloseRequested ? HS_CLOSED : HS_OPEN;

        if (isStalled && lastCloseRequested) {
            // 堵转点距离闭合零点的距离（与符号无关，适配任意安装方向）
            float dist = angle - ClosedAngle;
            if (dist < 0) dist = -dist;
            return (dist > epsilon) ? HS_GRIPPED : HS_CLOSED;
        }
        if (isStalled && !lastCloseRequested)
            return HS_OPEN;

        return lastCloseRequested ? HS_CLOSED : HS_OPEN;
    }

private:
    bool is_cali = 0;
    // 最后一次发出的方向语义：true = 关爪意图（!HD_C / SetPercentToAngle<50），false = 张爪意图。
    // 默认 true：上电后未发命令时若被外力推开，#GETJPOS 报 H:0（比报 H:1 更安全）。
    bool lastCloseRequested = true;
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
    CtrlStepMotor* motorJ[8] = {nullptr};
//    CtrlStepMotor* hand2 = {nullptr};
    StepHand* hand2 = {nullptr};

    // CAN 健康可观测性 (T10)
    volatile uint32_t canDroppedFrames = 0;   // id==0 或 id>7 的丢弃帧计数
    volatile uint32_t canRxTotal = 0;         // 总接收帧计数


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
    void LoadRGBModeFromEEPROM();
    void SaveRGBModeToEEPROM();
    // 自动模式子状态驱动
    void SetRGBAutoSubState(uint8_t subState);
    void NotifyWorkStart();
    void NotifyWorkDone();
    void AutoModeIdleTick(uint32_t deltaMs);
    // ---- 在线检测 ----
    bool CheckNodeOnline(uint8_t node);          // 单节点检测（阻塞等回调）
    bool CheckAllNodesOnline();                   // 全部节点检测
    void OnlineCheckTick(uint32_t deltaMs);       // 定时检查 tick（5 分钟）
    bool AllNodesVerified() const { return allNodesOnline; }
    void CalibrateHomeOffset();
    void Homing();
    void Resting();
    // !START：使能机械臂 + 使能手爪 + 回零点姿态（REST_POSE）。会重置 isHomed=false。
    void Startup();
    // !CLOSE：先回零点姿态 → 然后失能机械臂+手爪。会重置 isHomed=false。
    void Shutdown();
    // 手爪动作的 RGB 联动 hook。open=true → 工作状态(绿)，false → 完成状态(青，5s回待机)。
    void OnHandActionRGBHook(bool _open);
    bool IsMoving();
    bool IsEnabled();
    bool IsHomed() const { return isHomed; }
    float GetJointSpeed() const { return jointSpeed; }
    void SetCommandMode(uint32_t _mode);

    // ---- 异步系统命令（解决阻塞命令占用 UART 线程导致 STOP 无法抢占的问题）----
    enum SysCmd : uint8_t {
        SYSCMD_NONE = 0,
        SYSCMD_STARTUP,     // !START
        SYSCMD_HOME,        // !HOME
        SYSCMD_RESET,       // !RESET
        SYSCMD_SHUTDOWN,    // !CLOSE
    };
    struct SysCmdMsg {
        SysCmd cmd;
        uint8_t channelType;  // StreamSink::CHANNEL_TYPE_xxx
    };
    void InitSysCmdThread();
    bool PostSysCmd(SysCmd cmd, uint8_t channelType);


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
        bool Pause();   // true=成功进入 PAUSED；false=当前状态不允许
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
    uint32_t rgbMode = 11;          // 默认自动模式
    uint32_t autoIdleTickMs = 0;     // 空闲计时（ms），5秒无指令回待机
    static constexpr uint32_t AUTO_IDLE_THRESHOLD_MS = 5000;
    // ---- 在线检测 ----
    bool allNodesOnline = false;              // 所有节点在线标志
    uint32_t onlineCheckAccumMs = 0;          // 定时检查累计
    static constexpr uint32_t ONLINE_CHECK_INTERVAL_MS = 5 * 60 * 1000; // 5 分钟
};


#endif //REF_STM32F4_FW_DUMMY_ROBOT_H
