"""验证真实上位机事务隔离、优先指令及 PIDV 协议，不访问串口。"""
import asyncio

import pytest

from app.core import protocol as P
from app.core.robot import Robot, RobotError
from tests.test_simulation import FakeTransport
from pydantic import ValidationError
from app.api.robot import PidBody, RawCmd


def unpack(wire):
    prefix, command = wire.split(" ", 1)
    return int(prefix[2:]), command


@pytest.fixture
async def correlated_pair():
    transport = FakeTransport()
    def realtime_reply(wire):
        request, command = unpack(wire)
        if command.startswith("#ONLINE"):
            if command == "#ONLINE 7":
                return [f"R:{request} Online OK node=7 OL", f"R:{request} Online End"]
            return [
                f"R:{request} Online OK all_online",
                *[f"R:{request} {'HAND' if n == 7 else 'J'}[{n}] OL" for n in range(1, 8)],
                f"R:{request} Online End",
            ]
        if command.startswith("#GETLIMIT"):
            nodes = [int(command.split()[1])] if len(command.split()) == 2 and command.split()[1].isdigit() else range(1, 8)
            lines = [f"R:{request} GetLimit Begin STATE:NORMAL"]
            for n in nodes:
                lines.extend([
                    f"R:{request} LIMIT NODE:{n} ACC:10.000000 APPLIED:10.000000",
                    f"R:{request} LIMIT NODE:{n} SPEED:20.000000 APPLIED:20.000000",
                    f"R:{request} LIMIT NODE:{n} CURRENT:1.000000 APPLIED:1.000000",
                ])
            lines.append(f"R:{request} GetLimit End")
            return lines
        return []
    transport.on(lambda wire: unpack(wire)[1].startswith(("#ONLINE", "#GETLIMIT")), realtime_reply)
    robot = Robot(transport, correlated=True)
    await robot.start()
    try:
        yield robot, transport
    finally:
        await robot.stop()


async def settle():
    for _ in range(8):
        await asyncio.sleep(0)


async def test_stop_and_query_during_inflight_action(correlated_pair):
    robot, transport = correlated_pair

    def reply(wire):
        request, command = unpack(wire)
        if command.startswith("&"):
            return [f"R:{request} Move Accepted Q:1"]
        if command == "!STOP":
            return [f"R:{request} Stop OK STATE:EMERGENCY_STOP"]
        if command == "#GETSPEED":
            return [f"R:{request} GetSpeed OK SPEED:100"]
        if command == "!RESUME":
            return [f"R:{request} Resume OK FROM:EMERGENCY_STOP STATE:NORMAL"]
        return []

    transport.on(lambda _: True, reply)
    action = asyncio.create_task(robot.move_j([20, 0, 90, 0, 0, 0], 100))
    await settle()
    assert not action.done()
    request, _ = next(unpack(wire) for wire in transport.tx_log if unpack(wire)[1].startswith("&"))
    await asyncio.wait_for(robot.raw("!STOP"), 0.3)
    assert robot.state.motion_state == "EMERGENCY_STOP"
    assert await asyncio.wait_for(robot.get_speed(), 0.3) == 100
    assert not action.done()
    await robot.resume()
    transport.emit_async_event(f"R:{request} Move OK STATE:NORMAL")
    await asyncio.wait_for(action, 0.3)


async def test_same_tag_late_reply_cannot_complete_next_request(correlated_pair):
    robot, transport = correlated_pair
    with pytest.raises(RobotError):
        await robot.raw("#GETSPEED", timeout=0.02)
    previous, _ = unpack(transport.tx_log[-1])

    def reply(wire):
        current, _ = unpack(wire)
        return [f"R:{previous} GetSpeed OK SPEED:1", f"R:{current} GetSpeed OK SPEED:88"]

    transport.on(lambda _: True, reply)
    assert await robot.get_speed() == 88


async def test_priority_ignores_ordinary_command_lock(correlated_pair):
    robot, transport = correlated_pair
    transport.on(lambda _: True, lambda wire: [f"R:{unpack(wire)[0]} Stop OK STATE:EMERGENCY_STOP"])
    async with robot._cmd_lock:
        await asyncio.wait_for(robot.raw("!STOP"), 0.3)
    assert len(transport.tx_log) == 1


async def test_acceptance_does_not_hide_execution_failure(correlated_pair):
    robot, transport = correlated_pair

    def reply(wire):
        request, _ = unpack(wire)
        return [f"R:{request} Move Accepted Q:1", f"R:{request} Move Error: task_failed state=NORMAL"]

    transport.on(lambda _: True, reply)
    with pytest.raises(RobotError, match="task_failed"):
        await robot.move_j([0, 0, 90, 0, 0, 0], 30)


async def test_pause_suspends_action_deadline(correlated_pair):
    robot, transport = correlated_pair
    transport.on(lambda _: True, lambda _: [])
    action = asyncio.create_task(robot.raw("&0,0,90,0,0,0,30", timeout=0.04))
    await settle()
    request, _ = unpack(transport.tx_log[-1])
    transport.emit_async_event("R:1 Pause OK STATE:PAUSED")
    await asyncio.sleep(0.1)
    assert not action.done()
    transport.emit_async_event("R:2 Resume OK FROM:PAUSED STATE:NORMAL")
    transport.emit_async_event(f"R:{request} Move OK STATE:NORMAL")
    await action


async def test_queue_wait_does_not_consume_action_deadline(correlated_pair):
    robot, transport = correlated_pair
    transport.on(lambda _: True, lambda wire: [f"R:{unpack(wire)[0]} Move Accepted Q:16"])
    action = asyncio.create_task(robot.raw("&0,0,90,0,0,0,30", timeout=0.04))
    await settle()
    request, _ = unpack(transport.tx_log[-1])
    await asyncio.sleep(0.1)
    assert not action.done()
    transport.emit_async_event(f"R:{request} Task Executing STATE:NORMAL")
    transport.emit_async_event(f"R:{request} Move OK STATE:NORMAL")
    await action


async def test_pid_all_nodes_missing_values_are_absent(correlated_pair):
    robot, transport = correlated_pair

    def reply(wire):
        request, command = unpack(wire)
        assert command == "#GETPID"
        return [f"R:{request} {line}" for line in ["J1  OL:", "KP:12", "KI:34", "KD:56", "KV:78", "J7  NL:", "KP:9", "GetPid OK STATE:PAUSED"]]

    transport.on(lambda _: True, reply)
    result = await robot.get_dce(pid=True)
    assert result["nodes"][1] == {"online": True, "kp": 12, "ki": 34, "kd": 56, "kv": 78}
    assert result["nodes"][7] == {"online": False, "kp": 9}


@pytest.mark.parametrize("values, suffix", [
    ({"kp": 10}, "kp 10"),
    ({"kp": 10, "ki": 20, "kd": 30, "kv": 40}, "10 20 30 40"),
])
async def test_pid_set_single_and_all(correlated_pair, values, suffix):
    robot, transport = correlated_pair

    def reply(wire):
        request, command = unpack(wire)
        assert command == f"#SETPID 7 {suffix}"
        return [f"R:{request} SetPid OK node=7"]

    transport.on(lambda _: True, reply)
    assert (await robot.set_pid(7, values)).ok


@pytest.mark.parametrize("values", [{}, {"kp": -1}, {"ki": 2**31}, {"kp": 1, "ki": 2}, {"kp": True}, {"p": 1}])
async def test_bad_pid_never_sent(correlated_pair, values):
    robot, transport = correlated_pair
    with pytest.raises(RobotError):
        await robot.set_pid(1, values)
    assert not transport.tx_log


async def test_raw_motion_command_uses_realtime_limit_and_returns_warning(correlated_pair):
    robot, transport = correlated_pair

    def reply(wire):
        request, command = unpack(wire)
        assert command.endswith(",20")
        return [f"R:{request} Move OK"]

    transport.on(lambda wire: unpack(wire)[1].startswith("&"), reply)
    response = await robot.raw("&0,0,90,0,0,0,100")
    assert transport.tx_log[-1].endswith(",20")
    assert "20" in robot.last_limit_warning
    assert response.applied_command.endswith(",20")
    assert "20" in response.warning


async def test_raw_hand_current_uses_realtime_limit(correlated_pair):
    robot, transport = correlated_pair

    def limit_reply(wire):
        request, _ = unpack(wire)
        return [
            f"R:{request} GetLimit Begin STATE:NORMAL",
            f"R:{request} LIMIT NODE:7 ACC:10.000000 APPLIED:10.000000",
            f"R:{request} LIMIT NODE:7 SPEED:20.000000 APPLIED:20.000000",
            f"R:{request} LIMIT NODE:7 CURRENT:0.500000 APPLIED:0.500000",
            f"R:{request} GetLimit End",
        ]

    transport.on(lambda wire: unpack(wire)[1] == "#GETLIMIT 7", limit_reply)

    def reply(wire):
        request, command = unpack(wire)
        assert command == "!HD_I 0.5"
        return [f"R:{request} HD_I OK current=0.5"]

    transport.on(lambda wire: unpack(wire)[1].startswith("!HD_I"), reply)
    response = await robot.raw("!HD_I 1.2")
    assert transport.tx_log[-1].endswith("!HD_I 0.5")
    assert "0.5" in robot.last_limit_warning
    assert response.applied_command == "!HD_I 0.5"
    assert "0.5" in response.warning


async def test_raw_limit_warning_is_bound_to_each_response(correlated_pair):
    robot, transport = correlated_pair

    def reply(wire):
        request, command = unpack(wire)
        return [f"R:{request} Move OK command={command}"]

    transport.on(lambda wire: unpack(wire)[1].startswith("&"), reply)
    first, second = await asyncio.gather(
        robot.raw("&0,0,90,0,0,0,100"),
        robot.raw("&0,0,90,0,0,0,10"),
    )
    assert first.applied_command.endswith(",20")
    assert "20" in first.warning
    assert second.applied_command.endswith(",10")
    assert second.warning == ""


@pytest.mark.parametrize("command", ["!HOME\n!CLOSE", "#GETSPEED\r!STOP", "!DISABLE"])
async def test_bypass_commands_never_sent(correlated_pair, command):
    robot, transport = correlated_pair
    with pytest.raises(RobotError):
        await robot.raw(command)
    assert not transport.tx_log


@pytest.mark.parametrize("value", [float("nan"), float("inf"), -float("inf")])
def test_nonfinite_motion_rejected(value):
    with pytest.raises(ValueError):
        P.cmd_move_j([value, 0, 90, 0, 0, 0], 30)
    with pytest.raises(ValueError):
        P.cmd_move_l([0, 0, 0, 0, 0, 0], value)


@pytest.mark.parametrize("value", [True, 1.5, "12"])
def test_pid_http_schema_does_not_coerce_values(value):
    with pytest.raises(ValidationError):
        PidBody(node=1, values={"kp": value})


@pytest.mark.parametrize("value", [0, -1, float("nan"), float("inf")])
def test_raw_http_schema_rejects_invalid_timeout(value):
    with pytest.raises(ValidationError):
        RawCmd(command="!HOME", timeout=value)


async def test_unknown_raw_command_is_business_error(correlated_pair):
    robot, transport = correlated_pair
    with pytest.raises(RobotError, match="不支持"):
        await robot.raw("!UNKNOWN")
    assert not transport.tx_log


def test_websocket_stop_is_received_while_home_is_pending():
    from fastapi.testclient import TestClient
    from app.main import app

    transport = FakeTransport()
    transport.connected = True

    def reply(wire):
        request, command = unpack(wire)
        if command == "!HOME":
            return [f"R:{request} Home Accepted Q:1"]
        if command == "!STOP":
            return [f"R:{request} Stop OK STATE:EMERGENCY_STOP"]
        return []

    transport.on(lambda _: True, reply)
    with TestClient(app) as client:
        async def setup():
            robot = Robot(transport, correlated=True)
            await robot.start()
            app.state.manager._robot = robot

        client.portal.call(setup)
        with client.websocket_connect("/ws") as websocket:
            assert websocket.receive_json()["type"] == "state"
            websocket.send_json({"type": "raw", "command": "!HOME"})
            websocket.send_json({"type": "raw", "command": "!STOP"})
            for _ in range(5):
                message = websocket.receive_json()
                if message.get("tag") == "Stop":
                    assert "EMERGENCY_STOP" in message["rest"]
                    break
            else:
                pytest.fail("STOP 未能在 HOME 等待期间完成")
        assert len(transport.tx_log) == 2


def test_websocket_raw_returns_limit_warning_for_submitted_command():
    from fastapi.testclient import TestClient
    from app.main import app

    transport = FakeTransport()
    transport.connected = True

    def reply(wire):
        request, command = unpack(wire)
        if command == "#ONLINE 7":
            return [f"R:{request} Online OK node=7 OL", f"R:{request} Online End"]
        if command.startswith("#ONLINE"):
            return [
                f"R:{request} Online OK all_online",
                *[f"R:{request} {'HAND' if n == 7 else 'J'}[{n}] OL" for n in range(1, 8)],
                f"R:{request} Online End",
            ]
        if command.startswith("#GETLIMIT"):
            nodes = [int(command.split()[1])] if len(command.split()) == 2 and command.split()[1].isdigit() else range(1, 8)
            lines = [f"R:{request} GetLimit Begin STATE:NORMAL"]
            for n in nodes:
                lines.extend([
                    f"R:{request} LIMIT NODE:{n} ACC:10.000000 APPLIED:10.000000",
                    f"R:{request} LIMIT NODE:{n} SPEED:20.000000 APPLIED:20.000000",
                    f"R:{request} LIMIT NODE:{n} CURRENT:1.000000 APPLIED:1.000000",
                ])
            lines.append(f"R:{request} GetLimit End")
            return lines
        if command.startswith("&"):
            return [f"R:{request} Move OK"]
        return []

    transport.on(lambda _: True, reply)
    with TestClient(app) as client:
        async def setup():
            robot = Robot(transport, correlated=True)
            await robot.start()
            app.state.manager._robot = robot

        client.portal.call(setup)
        with client.websocket_connect("/ws") as websocket:
            assert websocket.receive_json()["type"] == "state"
            websocket.send_json({"type": "raw", "command": "&0,0,90,0,0,0,100"})
            for _ in range(8):
                message = websocket.receive_json()
                if message.get("type") == "command_result":
                    assert message["applied_command"].endswith(",20")
                    assert "20" in message["warning"]
                    break
            else:
                pytest.fail("WebSocket 未返回本次原始指令的限幅提醒")
