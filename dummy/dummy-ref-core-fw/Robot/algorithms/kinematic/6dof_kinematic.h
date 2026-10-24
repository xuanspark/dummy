// ============================================================================
//  6dof_kinematic.h —— 6 自由度机械臂运动学求解器
// ----------------------------------------------------------------------------
//  - 构造时传入 DH 参数（a1..a6 连杆长度），内部完成正逆解所需常量
//  - SolveFK(joints, &pose) : 正运动学，关节角 → 笛卡尔位姿
//  - SolveIK(pose, lastJoints, &solves) : 逆运动学，最多 8 组解
//  - Joint6D_t / Pose6D_t / IKSolves_t 结构体定义在本头文件
//  - 单位约定：长度 m，角度 deg
// ============================================================================
#ifndef DOF6_KINEMATIC_SOLVER_H
#define DOF6_KINEMATIC_SOLVER_H

#include "stm32f405xx.h"
#include "arm_math.h"
#include "memory.h"

class DOF6Kinematic
{
private:
    const float RAD_TO_DEG = 57.295777754771045f;   // 弧度与角度互转常量

    // ----- DH 参数（6个连杆长度，单位 m） -----
    struct ArmConfig_t
    {
        float L_BASE;     // 基座高度
        float D_BASE;     // 基座偏移
        float L_ARM;      // 上臂长
        float L_FOREARM;  // 前臂长
        float D_ELBOW;    // 手肘偏移
        float L_WRIST;    // 手腕长
    };
    ArmConfig_t armConfig;

    // DH 参数表，每轴一行：[home_offset, d, a, alpha]。构造函数里填充。
    float DH_matrix[6][4] = {0}; // home,d,a,alpha
    // 预计算的关节间偏移向量（3D），IK 中性能热点
    float L1_base[3] = {0};
    float L2_arm[3] = {0};
    float L3_elbow[3] = {0};
    float L6_wrist[3] = {0};

    // 预计算的关节间距离与其平方，用于 IK 三角函数加速
    float l_se_2;     // |shoulder → elbow|^2
    float l_se;       // |shoulder → elbow|
    float l_ew_2;     // |elbow → wrist|^2
    float l_ew;       // |elbow → wrist|
    float atan_e;     // 手肘偏移对应的补角

public:
    // 6 关节角。a[0..5] 对应 J1..J6，单位 deg。
    struct Joint6D_t
    {
        Joint6D_t()
        = default;

        Joint6D_t(float a1, float a2, float a3, float a4, float a5, float a6)
            : a{a1, a2, a3, a4, a5, a6}
        {}

        float a[6];

        // 重载减法：逐关节偏移量，常用于 MoveJ 取轴偏移最大者。
        friend Joint6D_t operator-(const Joint6D_t &_joints1, const Joint6D_t &_joints2);
    };

    // 笛卡尔位姿。XYZ 平移（m）+ ABC 欧拉角（deg，ZYX 顺序）+ R 旋转矩阵。
    struct Pose6D_t
    {
        Pose6D_t()
        = default;

        Pose6D_t(float x, float y, float z, float a, float b, float c)
            : X(x), Y(y), Z(z), A(a), B(b), C(c), hasR(false)
        {}

        float X{}, Y{}, Z{};      // 平移，单位 m（上层会转 mm）
        float A{}, B{}, C{};      // ZYX 欧拉角，单位 deg
        float R[9]{};             // 旋转矩阵（3×3，行优先平铺）

        // 若 R 已由 SolveFK 填好（自动置 true），SolveIK 可跳过从 A/B/C 重建矩阵。
        // 手动填 A/B/C 者必须设为 false，以保证 IK 走完整路径。
        bool hasR{};
    };

    // IK 返回结果：最多 8 组解（三个二重解果的组合）。
    // solFlag[i] = {shoulder, elbow, wrist}、每位 0/1 表示是否在该分支。
    struct IKSolves_t
    {
        Joint6D_t config[8];      // 8 组解的关节角
        char solFlag[8][3];       // 8 组解的身份标识
    };

    // 构造函数：传入 6 个连杆长度，内部填充 DH_matrix 与预计算量。
    DOF6Kinematic(float L_BS, float D_BS, float L_AM, float L_FA, float D_EW, float L_WT);

    // FK：关节角 → 笛卡尔位姿。会同时填 R 旋转矩阵与 A/B/C 欧拉角。
    bool SolveFK(const Joint6D_t &_inputJoint6D, Pose6D_t &_outputPose6D);

    // IK：笛卡尔位姿 → 8 组关节角解。_lastJoint6D 仅用于身份预选（在列举同时供参）。
    bool SolveIK(const Pose6D_t &_inputPose6D, const Joint6D_t &_lastJoint6D, IKSolves_t &_outputSolves);
};

#endif //DOF6_KINEMATIC_SOLVER_H
