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

        //new
        if (id>6)
        {
            switch (cmd)
            {
                case 0x23:
                    dummy.hand2->UpdateAngleCallback(*(float*) (data), data[4]);
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