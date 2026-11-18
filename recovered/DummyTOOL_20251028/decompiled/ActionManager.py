# uncompyle6 version 3.9.3
# Python bytecode version base 3.7.0 (3394)
# Decompiled from: Python 3.13.13 (tags/v3.13.13:01104ce, Apr  7 2026, 19:25:48) [MSC v.1944 64 bit (AMD64)]
# Embedded file name: ActionManager.py
import json, queue, time
from typing import List, Dict, Any
from PyQt5.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QListWidget, QTableWidget, QTableWidgetItem, QLineEdit, QPushButton, QMessageBox, QInputDialog
from PyQt5.QtCore import QTimer, Qt, pyqtSignal, QObject, QThread
from dataclasses import dataclass
from subform import Ui_Form

@dataclass
class Point:
    x: float
    y: float
    z: float
    rx: float
    ry: float
    rz: float
    h: float

    def to_dict(self) -> Dict[(str, float)]:
        return {'x':self.x, 
         'y':self.y,  'z':self.z,  'rx':self.rx, 
         'ry':self.ry,  'rz':self.rz,  'h':self.h}

    @classmethod
    def from_dict(cls, data: Dict[(str, float)]) -> "Point":
        return cls(**data)


class Action:

    def __init__(self, name: str, points: List[Point]=None):
        self.name = name
        self.points = points if points else []

    def to_dict(self) -> Dict[(str, Any)]:
        return {'name':self.name, 
         'points':[point.to_dict() for point in self.points]}

    @classmethod
    def from_dict(cls, data: Dict[(str, Any)]) -> "Action":
        return cls(data["name"], [Point.from_dict(point) for point in data["points"]])


class ActionManagerSignals(QObject):
    start_recording = pyqtSignal(int)
    stop_recording = pyqtSignal()
    execute_action = pyqtSignal(list)
    stop_action = pyqtSignal()


class ActionManager:

    def __init__(self, file_path: str):
        self.file_path = file_path
        self.actions = load_actions(file_path)

    def add_action(self, action: Action):
        self.actions.append(action)
        self.save()

    def delete_action(self, name: str):
        self.actions = [action for action in self.actions if action.name != name]
        self.save()

    def save(self):
        save_actions(self.file_path, self.actions)


class RecordThread(QThread):
    point_recorded = pyqtSignal(Point)

    def __init__(self, coord_points_q):
        super().__init__()
        self.coord_points_q = coord_points_q
        self.is_recording = False

    def run(self):
        while self.is_recording:
            try:
                coord = self.coord_points_q.get(timeout=0.5)
                self.point_recorded.emit(coord)
            except queue.Empty:
                continue

            self.msleep(50)


def load_actions(file_path: str) -> List[Action]:
    try:
        with open(file_path, "r") as file:
            data = json.load(file)
            return [Action.from_dict(action) for action in data["actions"]]
    except FileNotFoundError:
        return []
    except Exception as e:
        try:
            QMessageBox.warning(None, "错误", f"无法读取文件: {e}")
            return []
        finally:
            e = None
            del e


def save_actions(file_path: str, actions: List[Action]):
    data = {"actions": [action.to_dict() for action in actions]}
    try:
        with open(file_path, "w") as file:
            json.dump(data, file, indent=4)
    except Exception as e:
        try:
            QMessageBox.warning(None, "错误", f"无法保存文件: {e}")
        finally:
            e = None
            del e


class ActionManagerWindow(QWidget):

    def __init__(self, action_manager):
        super().__init__()
        self.action_manager = action_manager
        self.signals = ActionManagerSignals()
        self.coord_points_q, self.action_points_q = queue.Queue(), queue.Queue()
        self.record_thread = RecordThread(self.coord_points_q)
        self.record_thread.point_recorded.connect(self.on_point_recorded)
        self.is_recording = False
        self.current_action = None
        self.is_running = False
        self.init_ui()

    def init_ui(self):
        self.ui = Ui_Form()
        self.ui.setupUi(self)
        self.action_list = self.findChild(QListWidget, "action_list")
        self.point_table = self.findChild(QTableWidget, "point_table")
        self.delete_button = self.findChild(QPushButton, "delete_button")
        self.record_button = self.findChild(QPushButton, "record_button")
        self.execute_button = self.findChild(QPushButton, "execute_button")
        self.action_list.itemClicked.connect(self.on_action_selected)
        self.delete_button.clicked.connect(self.on_delete_action)
        self.record_button.clicked.connect(self.toggle_recording)
        self.execute_button.clicked.connect(self.on_execute_action)
        self.update_action_list()
        self.current_execute_points = []

    def update_action_list(self):
        self.action_list.clear()
        for action in self.action_manager.actions:
            self.action_list.addItem(action.name)

    def on_action_selected(self, item):
        action_name = item.text()
        for action in self.action_manager.actions:
            if action.name == action_name:
                self.update_point_table(action.points)
                break

    def update_point_table(self, points: List[Point]):
        self.point_table.setRowCount(len(points))
        for i, point in enumerate(points):
            self.point_table.setItem(i, 0, QTableWidgetItem((f"{point.x:.3f}")))
            self.point_table.setItem(i, 1, QTableWidgetItem((f"{point.y:.3f}")))
            self.point_table.setItem(i, 2, QTableWidgetItem((f"{point.z:.3f}")))
            self.point_table.setItem(i, 3, QTableWidgetItem((f"{point.rx:.3f}")))
            self.point_table.setItem(i, 4, QTableWidgetItem((f"{point.ry:.3f}")))
            self.point_table.setItem(i, 5, QTableWidgetItem((f"{point.rz:.3f}")))

    def on_delete_action(self):
        selected_item = self.action_list.currentItem()
        if selected_item:
            current_row = self.action_list.row(selected_item)
            action_name = selected_item.text()
            self.action_manager.delete_action(action_name)
            self.update_action_list()
            self.point_table.clearContents()
            self.point_table.setRowCount(0)
            if current_row > 0:
                self.action_list.setCurrentRow(current_row - 1)

    def on_execute_action(self):
        if self.is_running == False:
            if self.is_recording == True:
                return
                selected_item = self.action_list.currentItem()
                if selected_item:
                    action_name = selected_item.text()
                    for action in self.action_manager.actions:
                        if action.name == action_name:
                            self.execute_points(action.points)
                            break

                    self.execute_button.setText("停止")
                    self.is_running = True
            else:
                QMessageBox.warning(self, "警告", "请选择一个动作！")
        else:
            self.is_running = False
            self.execute_button.setText("执行")
            self.signals.stop_action.emit()
            time.sleep(0.2)

    def execute_points(self, points: List[Point], is_start=True):
        if not points:
            QMessageBox.warning(self, "警告", "没有可执行的坐标点！")
            self.is_running = False
            self.execute_button.setText("执行")
            return
        self.signals.execute_action.emit(points)

    def toggle_recording(self):
        if self.is_recording:
            self.is_recording = False
            self.record_button.setText("开始录制")
            self.signals.stop_recording.emit()
            self.record_thread.is_recording = False
            self.record_thread.quit()
            self.record_thread.wait()
            action_name, ok = QInputDialog.getText(self, "保存动作", "请输入动作名称:", text="未命名")
            if ok and action_name:
                unique_name = self.generate_unique_action_name(action_name)
                self.current_action.name = unique_name
                self.action_manager.add_action(self.current_action)
                self.action_manager.save()
                QMessageBox.information(self, "保存成功", f"动作 '{unique_name}' 已保存！")
                self.current_action = None
                self.update_action_list()
        else:
            self.is_recording = True
            self.record_button.setText("停止录制")
            self.current_action = Action("未命名")
            self.signals.start_recording.emit(100)
            self.record_thread.is_recording = True
            self.record_thread.start()

    def on_point_recorded(self, point: Point):
        if self.is_recording:
            self.current_action.points.append(point)
            print(f"录制坐标: ({point.x}, {point.y}, {point.z}, {point.rx}, {point.ry}, {point.rz})")
            self.update_point_table(self.current_action.points)
            self.point_table.scrollToBottom()

    def generate_unique_action_name(self, base_name: str) -> str:
        """生成唯一的动作名称"""
        existing_names = {action.name for action in self.action_manager.actions}
        if base_name not in existing_names:
            return base_name
        suffix = 1
        while f"{base_name}_{suffix}" in existing_names:
            suffix += 1

        return f"{base_name}_{suffix}"
