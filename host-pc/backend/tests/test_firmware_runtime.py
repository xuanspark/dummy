"""在电脑上编译真实固件控制逻辑，使用硬件替身验证任务恢复和失能屏障。"""
from pathlib import Path
import shutil
import subprocess

import pytest


def test_native_firmware_runtime():
    compiler = shutil.which("g++")
    if not compiler:
        pytest.skip("未安装原生 C++ 编译器")
    root = Path(__file__).resolve().parents[3]
    firmware = root / "dummy/dummy-ref-core-fw"
    output = firmware / "build-control-v1.1"
    output.mkdir(exist_ok=True)
    executable = output / "control-runtime-test.exe"
    directories = [
        root / "host-pc/backend/tests/firmware/stubs",
        firmware / "Robot",
        firmware / "Robot/instances",
        firmware / "Bsp/gpio",
        firmware / "Bsp/communication",
    ]
    sources = [
        root / "host-pc/backend/tests/firmware/control_runtime_test.cpp",
        firmware / "Robot/instances/dummy_robot.cpp",
        firmware / "Robot/actuators/ctrl_step/ctrl_step.cpp",
        firmware / "UserApp/protocols/ascii_protocol.cpp",
        firmware / "Bsp/communication/ascii_processor.cpp",
        firmware / "Bsp/gpio/rgb.cpp",
    ]
    command = [compiler, "-std=c++17", "-O0", "-g"]
    for directory in directories:
        command += ["-I", str(directory)]
    command += [str(source) for source in sources] + ["-o", str(executable)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=90)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    executed = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
    assert executed.returncode == 0, executed.stdout + executed.stderr
