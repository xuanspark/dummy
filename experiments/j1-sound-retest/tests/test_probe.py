"""使用串口替身验证实验脚本的默认只读和播放前置条件。"""
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[1]))
import probe


class SerialProbe:
    def __init__(self, ready=True, version="1.4.1-exp3", j1_only=False, capability=3):
        self.ready = ready
        self.version = version
        self.j1_only = j1_only
        self.prepared = False
        self.capability = capability
        self.commands = []
        self.pending = bytearray()
        self.is_open = False

    def open(self):
        self.is_open = True

    def close(self):
        self.is_open = False

    @property
    def in_waiting(self):
        return len(self.pending)

    def write(self, data):
        transaction, command = data.decode("ascii").strip().split(" ", 1)
        self.commands.append(command)
        prefix = "R:" + transaction[2:] + " "
        if command == "#STATUS":
            lines = ["Status OK STATE:NORMAL", f"FW:{self.version} BUILD:test",
                     ("ENABLED:0 HOMED:0 ACTIVE:0 QUEUE:0" if self.j1_only else
                      f"ENABLED:1 HOMED:{int(self.ready)} ACTIVE:0 QUEUE:0"), "Status End"]
        elif command == "#J1_SOUND":
            mode = "J1_ONLY" if self.prepared else "ARM"
            lines = [f"J1Sound OK EXP:{self.capability} SOUND:0 STATE:NORMAL MODE:{mode} "
                     f"HOLD:{int(self.prepared)} READY:{int(self.prepared and self.ready)}"]
        elif command == "!J1_TEST_START":
            self.prepared = True
            lines = ["J1TestStart OK MODE:J1_ONLY HOLD:1 READY:1 STATE:NORMAL"]
        elif command == "#GETJPOS":
            lines = ["GetJPos OK 0 0 90 0 0 0 H:0"]
        else:
            lines = ["J1Sound Accepted HZ:1000 MS:200 MA:50", "J1Sound OK SOUND:2 STATE:NORMAL"]
        self.pending.extend("".join(prefix + line + "\n" for line in lines).encode("utf-8"))
        return len(data)

    def read(self, count):
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result


class ProbeTests(unittest.TestCase):
    def execute(self, device, *arguments):
        with patch.object(probe.serial, "Serial", return_value=device), \
             patch.object(sys, "argv", ["probe.py", *arguments]), \
             patch.object(probe.Path, "write_text"), patch("builtins.print"):
            probe.main()

    def test_default_is_read_only(self):
        device = SerialProbe()
        self.execute(device)
        self.assertEqual(device.commands, ["#STATUS", "#J1_SOUND", "#GETJPOS"])
        self.assertFalse(device.is_open)

    def test_unhomed_never_plays(self):
        device = SerialProbe(ready=False)
        with self.assertRaises(RuntimeError):
            self.execute(device, "--play")
        self.assertTrue(all(command.startswith("#") for command in device.commands))
        self.assertFalse(device.is_open)

    def test_wrong_firmware_stops_at_status(self):
        device = SerialProbe(version="1.1.6")
        with self.assertRaises(RuntimeError):
            self.execute(device, "--play")
        self.assertEqual(device.commands, ["#STATUS"])

    def test_one_explicit_pulse_and_no_motion(self):
        device = SerialProbe()
        self.execute(device, "--play")
        self.assertEqual([command for command in device.commands if command.startswith("!")],
                         ["!J1_SOUND 1000 200 50 D_SINE"])

    def test_j1_flag_alone_remains_read_only(self):
        device = SerialProbe(version="1.4.1-exp3", j1_only=True)
        self.execute(device, "--j1-only")
        self.assertTrue(all(command.startswith("#") for command in device.commands))

    def test_single_j1_requires_explicit_prepare(self):
        device = SerialProbe(version="1.4.1-exp3", j1_only=True)
        with self.assertRaises(RuntimeError):
            self.execute(device, "--j1-only", "--play")
        self.assertTrue(all(command.startswith("#") for command in device.commands))

    def test_single_j1_prepare_never_sends_arm_motion(self):
        device = SerialProbe(version="1.4.1-exp3", j1_only=True)
        self.execute(device, "--j1-only", "--prepare", "--play")
        self.assertEqual([command for command in device.commands if command.startswith("!")],
                         ["!J1_TEST_START", "!J1_SOUND 1000 200 50 D_SINE"])

    def test_old_board_cannot_prepare_single_j1(self):
        device = SerialProbe(version="1.4.1-exp3", j1_only=True, capability=2)
        with self.assertRaises(RuntimeError):
            self.execute(device, "--j1-only", "--prepare", "--play")
        self.assertTrue(all(command.startswith("#") for command in device.commands))

    def test_unready_single_j1_cannot_play_after_prepare(self):
        device = SerialProbe(ready=False, version="1.4.1-exp3", j1_only=True)
        with self.assertRaises(RuntimeError):
            self.execute(device, "--j1-only", "--prepare", "--play")
        self.assertTrue(all(not command.startswith("!J1_SOUND") for command in device.commands))

    def test_explicit_q_square_is_forwarded_once(self):
        device = SerialProbe(j1_only=True)
        self.execute(device, "--j1-only", "--prepare", "--play", "--wave", "Q_SQUARE")
        self.assertEqual([command for command in device.commands if command.startswith("!J1_SOUND")],
                         ["!J1_SOUND 1000 200 50 Q_SQUARE"])

    def test_previous_experiment_is_rejected_before_actions(self):
        device = SerialProbe(version="1.3.1-exp2", j1_only=True, capability=2)
        with self.assertRaises(RuntimeError):
            self.execute(device, "--j1-only", "--prepare", "--play")
        self.assertEqual(device.commands, ["#STATUS"])


if __name__ == "__main__":
    unittest.main()
