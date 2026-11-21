"""冻结硬件协议的上位机接口描述。

该模块只描述已经存在的硬件能力，不主动发送设备命令；这样前端可以先
依据稳定契约开发，尚未完成现场验收的功能仍然保留在论证清单中。
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any


HARDWARE_INTERFACE_VERSION = "1.1"
FIRMWARE_BASELINE_VERSION = "1.2.30"
FIRMWARE_CANDIDATE_VERSION = "1.3.0"


@dataclass(frozen=True)
class HardwareInterface:
    """描述主控、关节节点和手爪节点之间的稳定接口。"""

    connected: bool
    motion_state: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "interface_version": HARDWARE_INTERFACE_VERSION,
            "firmware": {
                "baseline": FIRMWARE_BASELINE_VERSION,
                "candidate": FIRMWARE_CANDIDATE_VERSION,
                "policy": "以候选固件完成编译和现场复测后再替换冻结基线",
            },
            "connection": {
                "connected": self.connected,
                "motion_state": self.motion_state,
            },
            "nodes": {
                "controller": {"id": 0, "bus": "CAN1", "role": "主控"},
                "joints": {"ids": [1, 2, 3, 4, 5, 6], "bus": "CAN1", "role": "关节电机"},
                "gripper": {"id": 7, "bus": "CAN1", "role": "手爪电机"},
            },
            "commands": {
                "lifecycle": ["!START", "!HOME", "!STOP", "!PAUSE", "!RESUME", "!CLOSE", "!TEACH_ON", "!TEACH_OFF"],
                "motion": ["&j1,j2,j3,j4,j5,j6[,speed]", "@x,y,z,a,b,c[,speed]"],
                "gripper": ["!HD_O", "!HD_C", "!HD_EN", "!HD_DIS", "!HD_POS percent"],
                "diagnostics": ["#STATUS", "#ONLINE [node]", "#GETJPOS", "#GETLPOS"],
            },
            "diagnostics": {
                "status_fields": ["STATE", "CAN_RX", "CAN_DROP", "node online", "node age_ms"],
                "online_values": ["OL", "NL"],
                "error_categories": [
                    "bad_args",
                    "not_ready",
                    "state_blocked",
                    "queue_full",
                    "limit_exceeded",
                    "comm_timeout",
                    "unknown",
                ],
            },
            "compatibility": {
                "unknown_capability": "上位机必须忽略未知能力并保持已知命令可用",
                "version_rule": "先读取接口描述，再决定是否显示可选功能",
                "gripper_angle_semantics": "手爪百分比命令由固件映射到手爪角度，界面不直接写入角度",
                "teaching": "!TEACH_ON 进入失能示教并保留位置反馈；!TEACH_OFF 读取当前位置后同步使能",
            },
            "validation": {
                "verified": [
                    "六轴联合单点位和连续运动",
                    "急停、暂停、恢复和多命令连续发送",
                    "手爪开合与拔插后的在线恢复",
                    "速度 15 长时间联合运动",
                ],
                "pending": [
                    "带载测试最终数据",
                    "温升和电源压降最终数据",
                    "CAN 抗干扰量化数据",
                    "急停制动距离量化数据",
                    "手爪自动行程重复性证据",
                ],
            },
        }


def describe_hardware(*, connected: bool, motion_state: str) -> dict[str, Any]:
    """返回可供前端和自动化测试消费的硬件接口契约。"""
    return HardwareInterface(connected=connected, motion_state=motion_state).as_dict()
