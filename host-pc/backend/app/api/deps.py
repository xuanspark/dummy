"""Dependency-injection helpers for FastAPI routes."""
from __future__ import annotations

from fastapi import HTTPException, Request

from app.core.connection import ConnectionManager
from app.core.robot import Robot


def get_manager(request: Request) -> ConnectionManager:
    return request.app.state.manager


def get_robot(request: Request) -> Robot:
    mgr: ConnectionManager = request.app.state.manager
    if mgr.robot is None:
        raise HTTPException(status_code=409, detail="not connected; POST /api/connect first")
    return mgr.robot
