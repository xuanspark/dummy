// ============================================================================
//  cmd_protocol.cpp —— Fibre 二进制 RPC 接口（与 ASCII 协议并存）
// ----------------------------------------------------------------------------
//  - 通过 fibre/protocol.hpp 的 make_protocol_*** 模板向 PC 端 ref_tool 暴露
//    机器人内部对象与函数（Joint angle / temperature / RGB 等）
//  - HelperFunctions 提供少量代理函数（如 GetTemperatureHelper）
//  - 与 ascii_protocol.cpp 不冲突：ASCII 走文本一行，Fibre 走二进制 endpoint
// ============================================================================

#include "common_inc.h"

/*----------------- 1. 依赖的外部变量（可选） ------------------*/
extern DummyRobot dummy;
#define TEST_ADC_CHANNEL_0 0

// HelperFunctions: 为 fibre 远程调用提供代理函数。
// 主要是作为静态对象 staticFunctions，限定 make_protocol_function 需要
// “对象实例”参数、不能直接传顶层函数指针。
class HelperFunctions
{
public:
    /*--------------- 2. 辅助函数（可选） ----------------*/

    // 代理调用 AdcGetChipTemperature（本身是全局 C 函数，不能直接被 fibre 拿到）
    float GetTemperatureHelper()
    {
        return AdcGetChipTemperature();
    }

    // 代理读取 ADC 通道 0 的电压。仅作为“如何新增一条 fibre RPC”的示例。
    float GetVoltageHelper()
    {
        return AdcGetVoltage(TEST_ADC_CHANNEL_0);
    }


} staticFunctions;     // 唯一全局实例，供 MakeObjTree 引用


// MakeObjTree: 返回 fibre 顶层对象树。COMMIT_PROTOCOL 宏在本文件末尾展开
// 生成 CommitProtocol() 实现，该实现调 MakeObjTree 并 fibre_publish。
// 为 fibre 远程。。 PC 侧 ref_tool 连接后可看到的全部节点，都是在这里拼出来的。
static inline auto MakeObjTree()
{
    /*--------------- 3. 在此增加 fibre 可见的变量 / 函数 ----------------*/
    return make_protocol_member_list(
        // ---- 只读变量 ----
        make_protocol_ro_property("serial_number", &serialNumber),
        // ---- RPC 函数（3 参、为：名称、实例、成员函数指针）----
        make_protocol_function("get_temperature", staticFunctions, &HelperFunctions::GetTemperatureHelper),
        make_protocol_function("get_voltage", staticFunctions, &HelperFunctions::GetVoltageHelper),
        // ---- 嵌套对象：机器人全部接口（由 dummy.MakeProtocolDefinitions 拼出）----
        make_protocol_object("robot", dummy.MakeProtocolDefinitions())
    );
}


// COMMIT_PROTOCOL 定义在 communication.hpp：生成 CommitProtocol() 函数体，
// 调用 MakeObjTree() 拼出对象树后交给 fibre_publish。CommunicationTask 会调。
COMMIT_PROTOCOL
