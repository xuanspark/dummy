// ============================================================================
//  can_protocol.cpp —— ref 主控 CAN 回包分发
// ----------------------------------------------------------------------------
//  - 解码电机子板的标准帧回包并更新对应 CtrlStepMotor 状态
//  - 处理角度、DCE 参数、在线心跳、运动完成等实时反馈
//  - 该文件只消费子板回包，主动下发命令由 CtrlStepMotor 封装完成
// ============================================================================

#include "common_inc.h"


// Used for response CAN message.
static CAN_TxHeaderTypeDef txHeader =
    {
        .StdId = 0,
        .ExtId = 0,
        .IDE = CAN_ID_STD,
        .RTR = CAN_RTR_DATA,
        .DLC = 8,
        .TransmitGlobalTime = DISABLE
    };

extern DummyRobot dummy;

void OnCanMessage(CAN_context* canCtx, CAN_RxHeaderTypeDef* rxHeader, uint8_t* data)
{
    // Common CAN message callback, uses ID 32~0x7FF.
    if (canCtx->handle->Instance == CAN1)
    {
        uint8_t id = rxHeader->StdId >> 7; // 4Bits ID & 7Bits Msg
        uint8_t cmd = rxHeader->StdId & 0x7F; // 4Bits ID & 7Bits Msg

        dummy.canRxTotal++;

        // 节点 ID 边界过滤：id=0 为广播保留，id>7 为非法帧，均丢弃
        if (id == 0 || id > 7)
        {
            dummy.canDroppedFrames++;
            return;
        }

        // id == 7：手爪节点
        if (id == 7)
        {
            if (dummy.hand2 == nullptr)
                return;  // 手爪未初始化，丢弃
            switch (cmd)
            {
                case 0x23:
                    // 35hand 在 data[5] 填 isStalled；老 35motor 该字节恒为 0，向后兼容
                    dummy.hand2->UpdateAngleCallback(*(float*) (data), data[4], data[5]);
                    dummy.hand2->OnlineCallback();
                    break;
                case 0x25:
                    memcpy(&dummy.hand2->temperature, data, sizeof(uint32_t));
                    dummy.hand2->OnlineCallback();
                    break;
                case 0x27:
                case 0x28:
                case 0x29:
                case 0x2A:
                    dummy.hand2->DceParamCallback(cmd, *(int32_t*)(data));
                    break;
                default:
                    break;
            }
            return;
        }

        // id == 1~6：关节电机节点
        /*----------------------- ↓ Add Your CAN1 Packet Protocol Here ↓ ------------------------*/
        switch (cmd)
        {
            case 0x23:
                dummy.motorJ[id]->UpdateAngleCallback(*(float*) (data), data[4]);
                dummy.motorJ[id]->OnlineCallback();
                break;
            case 0x25:
                 memcpy(&dummy.motorJ[id]->temperature, data, sizeof(uint32_t));
                 dummy.motorJ[id]->OnlineCallback();
                break;
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2A:
                dummy.motorJ[id]->DceParamCallback(cmd, *(int32_t*)(data));
                break;
            default:
                break;
        }

        dummy.UpdateJointAnglesCallback();

    } else if (canCtx->handle->Instance == CAN2)
    {
        /*----------------------- ↓ Add Your CAN2 Packet Protocol Here ↓ ------------------------*/
    }
    /*----------------------- ↑ Add Your Packet Protocol Here ↑ ------------------------*/
}