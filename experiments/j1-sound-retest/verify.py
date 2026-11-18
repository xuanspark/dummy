"""离线编译真实实验波形、驱动与主控协议，不打开串口。"""
from pathlib import Path
import subprocess

from build import HERE, prepare


def main():
    source = prepare()
    ref = source / "dummy/dummy-ref-core-fw"
    motor = source / "dummy/dummy-42motor-fw"
    output = HERE / "build/verify"
    output.mkdir(parents=True, exist_ok=True)
    cases = [
        ("motor-runtime", [HERE / "tests/motor_stubs", motor / "Ctrl"],
         [HERE / "tests/motor_runtime_test.cpp", motor / "Ctrl/Motor/motor.cpp",
          motor / "Ctrl/Motor/motion_planner.cpp"]),
        ("sound-pulse", [motor / "Ctrl"], [HERE / "tests/sound_pulse_test.cpp",
                                          motor / "Ctrl/Driver/tb67h450_base.cpp"]),
        ("control-runtime", [source / "host-pc/backend/tests/firmware/stubs",
                             ref / "Robot", ref / "Robot/instances", ref / "Bsp/gpio",
                             ref / "Bsp/communication"],
         [HERE / "tests/control_runtime_test.cpp", ref / "Robot/instances/dummy_robot.cpp",
          ref / "Robot/actuators/ctrl_step/ctrl_step.cpp",
          ref / "UserApp/protocols/ascii_protocol.cpp",
          ref / "Bsp/communication/ascii_processor.cpp", ref / "Bsp/gpio/rgb.cpp"]),
    ]
    for name, includes, sources in cases:
        executable = output / f"{name}.exe"
        command = ["g++", "-std=c++17", "-O2", "-DJ1_SOUND_EXPERIMENT"]
        for include in includes:
            command += ["-I", str(include)]
        command += [str(path) for path in sources] + ["-o", str(executable)]
        subprocess.run(command, check=True, timeout=90)
        subprocess.run([str(executable)], check=True, timeout=30)
        print(f"离线检查通过：{name}", flush=True)


if __name__ == "__main__":
    main()
