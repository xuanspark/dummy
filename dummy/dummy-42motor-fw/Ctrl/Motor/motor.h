// ============================================================================
//  motor.h —— 顶层电机对象
// ----------------------------------------------------------------------------
//  - 聚合驱动 (DriverBase) 、编码器 (EncoderBase) 、运动规划器 (MotionPlanner) 与控制器 (Controller)
//  - Tick20kHz 是控制环主体，由 main.cpp 的 htim4 中断调用
//  - 位置单位：子步计数（1 圈 = MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS = 200 × 256 = 51200）
//  - DCE 控制器包含位置环 kp + 速度环 kv + 积分 ki + 微分 kd。
//  - PID 控制器仅调参 / 调试使用。
// ============================================================================
#ifndef CTRL_STEP_FW_MOTOR_H
#define CTRL_STEP_FW_MOTOR_H

#include "Motor/motion_planner.h"
#include "Sensor/Encoder/encoder_base.h"
#include "Driver/driver_base.h"

class Motor
{
public:
    Motor() :
        controller(&controllerInstance)
    {
        /****************** 默认配置（会被 main.cpp 中从 EEPROM 读取的配置覆盖） *******************/
        config.motionParams.encoderHomeOffset = 0;
        config.motionParams.caliCurrent = 2000;             // (mA)
        config.motionParams.ratedCurrent = 1000;            // (mA)
        config.motionParams.ratedCurrentAcc = 2 * 1000;     // (mA/s)
        config.motionParams.ratedVelocity = 30 * MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS;
        config.motionParams.ratedVelocityAcc = 1000 * MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS;

        config.ctrlParams.stallProtectSwitch = false;
        config.ctrlParams.pid =
            Controller::PID_t{
                .kp = 5,
                .ki = 30,
                .kd = 0
            };
        config.ctrlParams.dce =
            Controller::DCE_t{
                .kp = 200,
                .kv = 80,
                .ki = 300,
                .kd = 250
            };

        /*****************************************************/


        motionPlanner.AttachConfig(&config.motionParams);
        controller->AttachConfig(&config.ctrlParams);
    }


    const int32_t MOTOR_ONE_CIRCLE_HARD_STEPS = 200; // 1.8° 步进电机每圈硬步数
    const int32_t SOFT_DIVIDE_NUM = 256;             // 软件细分倍数
    const int32_t MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS = MOTOR_ONE_CIRCLE_HARD_STEPS * SOFT_DIVIDE_NUM;

    // 控制模式枚举。COMMAND_* 为 CAN/UART 指令驱动，PWM_* 为调试/越控连连，
    // STEP_DIR 为传统步进 / 方向接口（3 者仅可选一）。
    typedef enum
    {
        MODE_STOP,                  // 停止（不失能，仅不交换设点）
        MODE_COMMAND_POSITION,      // 位置环（设点：int32 子步计数）
        MODE_COMMAND_VELOCITY,      // 速度环（设点：int32 子步计数 / 秒）
        MODE_COMMAND_CURRENT,       // 电流环（设点：int32 mA）
        MODE_COMMAND_Trajectory,    // 轨迹点队列（位置 + 速度 几点一起入队）
        MODE_PWM_POSITION,          // 取调试：以 PWM 打入位置设点
        MODE_PWM_VELOCITY,          // 取调试：以 PWM 打入速度设点
        MODE_PWM_CURRENT,           // 取调试：以 PWM 打入电流设点
        MODE_STEP_DIR,              // 传统 STEP/DIR 接口（未实现）
    } Mode_t;

    // 运行状态枚举。Tick20kHz 里会根据环路误差 + 堆转保护设置。
    typedef enum
    {
        STATE_STOP,         // 未使能
        STATE_FINISH,        // 位置到达 / 速度稳定
        STATE_RUNNING,       // 运行中
        STATE_OVERLOAD,      // 过载警告
        STATE_STALL,         // 堆转保护触发
        STATE_NO_CALIB       // 未校准编码器（拒接运动命令）
    } State_t;


    // ----------------------------------------------------------------
    //  Controller —— 控制器嵌套类。负责从 encoder 读位置、计算误差、
    //  结合 motionPlanner 生成的轨迹输出电流。
    // ----------------------------------------------------------------
    class Controller
    {
    public:
        friend Motor;

        // PID 结构，仅调参/调试途。
        typedef struct
        {
            bool kpValid, kiValid, kdValid;          // 调参时可选使能各项
            int32_t kp, ki, kd;
            int32_t vError, vErrorLast;              // 当前误差与上一轮误差
            int32_t outputKp, outputKi, outputKd;    // 各项贡献
            int32_t integralRound;                   // 积分整数部分
            int32_t integralRemainder;               // 积分余下（提高分辨率）
            int32_t output;
        } PID_t;

        // DCE 结构（出厂默认控制器）。kp 位置环、kv 速度环、ki 积分、kd 微分。
        typedef struct
        {
            int32_t kp, kv, ki, kd;
            int32_t pError, vError;                  // 位置误差 + 速度误差
            int32_t outputKp, outputKi, outputKd;
            int32_t integralRound;
            int32_t integralRemainder;
            int32_t output;
        } DCE_t;

        typedef struct
        {
            PID_t pid;
            DCE_t dce;

            bool stallProtectSwitch;     // 是否启用堆转保护
        } Config_t;


        explicit Controller(Motor* _context)
        {
            context = _context;

            requestMode = MODE_STOP;
            modeRunning = MODE_STOP;
        }


        Config_t* config = nullptr;
        Mode_t requestMode;
        Mode_t modeRunning;
        State_t state = STATE_STOP;
        bool isStalled = false;


        void Init();                                                  // 初始化各环路参数与误差累计器
        void SetCtrlMode(Mode_t _mode);                               // 切换控制模式（会清除误差累计）
        void SetCurrentSetPoint(int32_t _cur);                        // 设电流设点 (mA)
        void SetVelocitySetPoint(int32_t _vel);                       // 设速度设点（子步 / 秒）
        void SetPositionSetPoint(int32_t _pos);                       // 设位置设点（子步计数）
        bool SetPositionSetPointWithTime(int32_t _pos, float _time);  // 设位置 + 到达时间
        float GetPosition(bool _isLap = false);                       // 返回当前位置（默认以圈为单位）
        float GetVelocity();                                          // 返回当前速度（r/s）
        float GetFocCurrent();                                        // 返回当前 FOC 环路采样电流 (A)
        void AddTrajectorySetPoint(int32_t _pos, int32_t _vel);       // 入队一个轨迹点
        void SetDisable(bool _disable);                               // 失能（该服不会清除设点）
        void SetBrake(bool _brake);                                   // 机械制动（双相短路）
        void ApplyPosAsHomeOffset();                                  // 将当前位置作为零点
        void ClearStallFlag();                                        // 清除堆转报警


    private:
        Motor* context;
        int32_t realLapPosition{};
        int32_t realLapPositionLast{};
        int32_t realPosition{};
        int32_t realPositionLast{};
        int32_t estVelocity{};
        int32_t estVelocityIntegral{};
        int32_t estLeadPosition{};
        int32_t estPosition{};
        int32_t estError{};
        int32_t focCurrent{};
        int32_t goalPosition{};
        int32_t goalVelocity{};
        int32_t goalCurrent{};
        bool goalDisable{};
        bool goalBrake{};
        int32_t softPosition{};
        int32_t softVelocity{};
        int32_t softCurrent{};
        bool softDisable{};
        bool softBrake{};
        bool softNewCurve{};
        int32_t focPosition{};
        uint32_t stalledTime{};
        uint32_t overloadTime{};
        bool overloadFlag{};


        void AttachConfig(Config_t* _config);
        void CalcCurrentToOutput(int32_t current);
        void CalcPidToOutput(int32_t _speed);
        void CalcDceToOutput(int32_t _location, int32_t _speed);
        void ClearIntegral() const;

        static int32_t CompensateAdvancedAngle(int32_t _vel);
    };


    // 顶层配置：运动规划器参数 + 控制器参数。整体作为 EEPROM 保存单元。
    struct Config_t
    {
        MotionPlanner::Config_t motionParams{};
        Controller::Config_t ctrlParams{};
    };
    Config_t config;

    MotionPlanner motionPlanner;        // 运动规划（梯形加减速、轨迹插补）
    Controller* controller = nullptr;   // 控制器指针（默认指 controllerInstance）
    EncoderBase* encoder = nullptr;     // 编码器（由 main.cpp 调 AttachEncoder 绑定到 mt6816）
    DriverBase* driver = nullptr;       // 驱动芯片（由 main.cpp 调 AttachDriver 绑定到 tb67h450）


    void Tick20kHz();                    // 控制环主体，由 htim4 以 50 µs 节拍调用
    void AttachEncoder(EncoderBase* _encoder);
    void AttachDriver(DriverBase* _driver);


private:
    Controller controllerInstance = Controller(this);


    void CloseLoopControlTick();
};

#endif
