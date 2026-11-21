"""硬件接口契约查询接口。"""
from __future__ import annotations

from fastapi import APIRouter, Request

from app.core.hardware_interface import describe_hardware


router = APIRouter(prefix="/api/hardware", tags=["hardware"])


@router.get("/interface")
async def hardware_interface(request: Request):
    """返回冻结硬件协议及当前连接快照，不会触发任何运动命令。"""
    manager = request.app.state.manager
    robot = manager.robot
    return describe_hardware(
        connected=robot is not None,
        motion_state=robot.state.motion_state if robot is not None else "DISCONNECTED",
    )
