"""离线验证恢复结果，仅调用选定方法及内存替身，不加载主程序或串口。"""

import __future__
import ast
from collections import Counter
import difflib
import hashlib
import io
import json
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
CHECKS = []


def check(name, condition):
    if not condition:
        raise AssertionError(name)
    CHECKS.append(name)


class Combo:
    def __init__(self, items=()):
        self.items = list(items)

    def count(self):
        return len(self.items)

    def itemText(self, index):
        return self.items[index]

    def addItem(self, value, *args):
        self.items.append(value)

    def findText(self, value):
        return self.items.index(value) if value in self.items else -1

    def clear(self):
        self.items.clear()


def method(module, class_name, name, **namespace):
    tree = ast.parse((ROOT / "src" / (module + ".py")).read_text(encoding="utf-8"))
    cls = next(item for item in tree.body if isinstance(item, ast.ClassDef) and item.name == class_name)
    node = next(item for item in cls.body if isinstance(item, ast.FunctionDef) and item.name == name)
    subset = ast.fix_missing_locations(ast.Module(body=[node], type_ignores=[]))
    exec(compile(subset, module, "exec", flags=__future__.annotations.compiler_flag), namespace)
    return namespace[name]


def fake_window(**values):
    result = SimpleNamespace(sent=[], errors=[], messages=[], **values)
    result.uni_send_data = result.sent.append
    result.log_error = result.errors.append
    result.log_message = result.messages.append
    return result


def verify_files():
    report = json.loads((ROOT / "evidence/recovery_manifest.json").read_text(encoding="utf-8"))
    diffs = []
    for item in report["modules"]:
        name = item["name"]
        raw = ROOT / "bytecode" / (name + ".marshal")
        check(name + ": 原始字节码校验", hashlib.sha256(raw.read_bytes()).hexdigest() == item["marshal_sha256"])
        original = (ROOT / "decompiled" / (name + ".py")).read_text(encoding="utf-8")
        edited = (ROOT / "src" / (name + ".py")).read_text(encoding="utf-8")
        check(name + ": 导出源码校验", hashlib.sha256((ROOT / "decompiled" / (name + ".py")).read_bytes()).hexdigest() == item["source_sha256"])
        for label, text in (("原始导出", original), ("修订源码", edited)):
            parsed = ast.parse(text, filename=name)
            compile(parsed, name, "exec")
            check(name + ": " + label + "语法与中文编码", chr(0xFFFD) not in text)
        constants = lambda text: Counter(
            node.value for node in ast.walk(ast.parse(text))
            if isinstance(node, ast.Constant) and isinstance(node.value, (str, bytes))
        )
        check(name + ": 文本与资源常量保持一致", constants(original) == constants(edited))
        diffs.extend(difflib.unified_diff(original.splitlines(True), edited.splitlines(True), fromfile="decompiled/" + name + ".py", tofile="src/" + name + ".py"))
    (ROOT / "evidence/repairs.diff").write_text("".join(diffs), encoding="utf-8")
    for path in (ROOT / "tools").glob("*.py"):
        compile(path.read_text(encoding="utf-8"), str(path), "exec")
        check(path.name + ": 工具语法", True)
    for item in json.loads((ROOT / "evidence/assets_manifest.json").read_text(encoding="utf-8")):
        relative = item["resource"][2:]
        data = (ROOT / "assets" / relative).read_bytes()
        check(relative + ": 资源校验", hashlib.sha256(data).hexdigest() == item["sha256"])


def verify_methods():
    main = lambda name, **scope: method("main_NEW_NOTEBOOK", "MainWindow", name, **scope)
    parse_response = main("append_received_data")
    window = fake_window(is_need_Lpos=True, curreentLpos=None)
    parse_response(window, "ok 1 2 3 4 5 6")
    check("旧坐标响应正确解析", window.curreentLpos == [1, 2, 3, 4, 5, 6] and not window.errors)

    send = main("uni_send_data")
    for serial in (None, SimpleNamespace(is_open=False)):
        window = fake_window(serial=serial)
        check("串口关闭时拒绝发送", send(window, "#GETJPOS") is False and bool(window.errors))
    written = []
    window = fake_window(serial=SimpleNamespace(is_open=True, write=written.append), txtSend=Combo())
    check("串口发送格式与历史记录", send(window, "#GETJPOS") is True and written == [b"#GETJPOS\r\n"] and window.txtSend.items == ["#GETJPOS"])

    pid = main("dummy_set_PID")
    for letter, value in (("P", 1000), ("I", 300), ("D", 250), ("V", 80)):
        window = fake_window(can_click=True, Foolproof=True)
        window.sender = lambda letter=letter: SimpleNamespace(text=lambda: "SET_DCE_K" + letter)
        window.comboBox_PID = SimpleNamespace(currentText=lambda: "J5")
        for parameter, number in (("P", 1000), ("I", 300), ("D", 250), ("V", 80)):
            setattr(window, "doubleSpinBox_" + parameter, SimpleNamespace(value=lambda number=number: number))
        pid(window)
        check("PID " + letter + " 参数指令", window.sent == ["#SET_DCE_K" + letter + " 5 " + str(value)])
    window = fake_window(can_click=False, Foolproof=True)
    pid(window)
    check("PID 冷却保护", not window.sent and bool(window.errors))

    timers = SimpleNamespace(singleShot=lambda *args: None)
    for can_click, foolproof, should_send in ((True, True, True), (False, False, True), (False, True, False)):
        for name in ("dummy_move", "dummy_button_history_pos", "dummy_button_action", "dummy_button_handoffset"):
            window = fake_window(can_click=can_click, Foolproof=foolproof)
            window.sender = lambda: SimpleNamespace(text=lambda: "A")
            window.indexs = ["A"]
            window.buttontexts = {0: "1,2,3,4,5,6"}
            window.global_cooldowns = {}
            window._reset_cooldown = lambda: None
            for button in ("pushButton_endp1", "pushButton_endp2", "pushButton_joint", "pushButton_GL", "pushButton_GJ"):
                setattr(window, button, object())
            if name == "dummy_move":
                window.sender = lambda: window.pushButton_GJ
            main(name, QTimer=timers)(window)
            check(name + ": 冷却分支 " + str((can_click, foolproof)), bool(window.sent) == should_send)

    refresh = main("refresh_serial_ports", sys=SimpleNamespace(platform="win32"))
    window = fake_window(btnOpen=SimpleNamespace(text=lambda: "Open"), cboxPortName=Combo())
    window.serial_ports_plus = lambda: ["COM3", "COM12", "COM2"]
    refresh(window)
    check("串口编号排序", window.cboxPortName.items == ["COM12", "COM3", "COM2"])

    texts, warnings, stopped = [], [], []
    selected = SimpleNamespace(text=lambda: "A")
    manager = fake_window(is_running=False, is_recording=False)
    manager.action_list = SimpleNamespace(currentItem=lambda: selected)
    manager.action_manager = SimpleNamespace(actions=[SimpleNamespace(name="A", points=[1, 2])])
    manager.execute_points = manager.sent.append
    manager.execute_button = SimpleNamespace(setText=texts.append)
    manager.signals = SimpleNamespace(stop_action=SimpleNamespace(emit=lambda: stopped.append(True)))
    execute = method("ActionManager", "ActionManagerWindow", "on_execute_action", QMessageBox=SimpleNamespace(warning=lambda *args: warnings.append(args)), time=SimpleNamespace(sleep=lambda *args: None))
    execute(manager)
    check("动作选择后执行", manager.sent == [[1, 2]] and manager.is_running)
    execute(manager)
    check("再次点击停止动作", not manager.is_running and bool(stopped))
    manager.is_recording = True
    execute(manager)
    check("录制中拒绝执行动作", len(manager.sent) == 1)
    manager.is_recording = False
    manager.action_list.currentItem = lambda: None
    execute(manager)
    check("未选择动作时提示", bool(warnings))

    history = main("load_history", os=SimpleNamespace(path=SimpleNamespace(exists=lambda _: True)), open=lambda *args, **kwargs: io.StringIO("a\nb\nc\n"))
    check("历史记录截取", history(SimpleNamespace(rootpath=""), 2) == ["b", "c"])


if __name__ == "__main__":
    verify_files()
    verify_methods()
    report = {
        "passed": len(CHECKS),
        "checks": CHECKS,
        "verification_scope": "静态编译与选定方法的内存替身测试",
        "qt_gui_launched": False,
        "executable_launched": False,
        "serial_ports_opened": False,
        "hardware_commands_sent": False,
    }
    (ROOT / "evidence/verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(str(len(CHECKS)) + " checks passed; no GUI or hardware access")
