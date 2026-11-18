# uncompyle6 version 3.9.3
# Python bytecode version base 3.7.0 (3394)
# Decompiled from: Python 3.13.13 (tags/v3.13.13:01104ce, Apr  7 2026, 19:25:48) [MSC v.1944 64 bit (AMD64)]
# Embedded file name: main_NEW_NOTEBOOK.py
import os, sys, threading
from enum import Enum
import serial
from PyQt5.QtGui import QPixmap, QIcon
import resources
from serial.tools import list_ports
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import QApplication, QMainWindow, QFileDialog, QMessageBox, QTableWidgetItem, QComboBox, QPushButton, QTextEdit, QCheckBox, QLabel, QInputDialog, QSlider
from PyQt5 import uic
from PyQt5.QtCore import QThread, pyqtSignal, QTimer, QMutex
import logging, glob, queue, time
from pathlib import Path
import configparser
from threading import Thread
SYMBOL_MAP = {'J':"&", 
 'L':"@"}

class HandCommand(Enum):
    DISABLE = "!HAND_DIS"
    ENABLE = "!HAND_EN"
    OPEN = "!HAND_O"
    CLOSE = "!HAND_C"


from ActivateAPP import ActivationManager, ActivationApp
Path("config").mkdir(parents=True, exist_ok=True)
logging.basicConfig(level=(logging.INFO), format="%(asctime)s - %(levelname)s - %(message)s", handlers=[
 logging.StreamHandler(),
 logging.FileHandler("config/app.log")])
from mainwindow import Ui_MainWindow
from subform import Ui_Form
import datetime
from ActionManager import *

class Logger:

    def __init__(self, text_edit: QTextEdit):
        self.text_edit = text_edit
        self.buffer = []
        self.timer = QTimer()
        self.timer.timeout.connect(self.flush)
        self.timer.start(500)

    def log(self, message):
        """普通日志（黑色），自动换行"""
        time_str = datetime.datetime.now().strftime("%H:%M:%S")
        self.buffer.append(f'<div style="color:white">[{time_str}] {message}</div>')

    def err(self, message):
        """错误日志（红色加粗），自动换行"""
        time_str = datetime.datetime.now().strftime("%H:%M:%S")
        self.buffer.append(f'<div style="color:red">[{time_str}] {message}</div>')

    def flush(self):
        if self.buffer:
            self.text_edit.append("\n".join(self.buffer))
            self.buffer.clear()


class SerialReceiver(QThread):
    data_received = pyqtSignal(str)

    def __init__(self, serial_instance):
        super().__init__()
        self.serial = serial_instance
        self.running = True

    def run(self):
        while self.running:
            try:
                if self.serial.is_open:
                    if self.serial.in_waiting:
                        str = self.serial.readline().decode("utf-8")
                        if str:
                            self.data_received.emit(str)
            except Exception as e:
                try:
                    print(f"Error reading serial data: {e}")
                    time.sleep(0.5)
                finally:
                    e = None
                    del e

    def stop(self):
        self.running = False


class MainWindow(QMainWindow, Ui_MainWindow):

    def append_received_data(self, data):
        """
        处理接收到的数据，直接记录字符串
        :param data: 接收到的原始数据（字符串类型）
        """

        def parse_ok_data(data):
            """
            解析 ok %.2f %.2f %.2f %.2f %.2f %.2f 格式的数据
            :param data: 接收到的数据字符串
            :return: 包含6个浮点数的列表，解析失败返回 None
            """
            try:
                parts = data.split()
                if len(parts) == 1:
                    return (len(parts), parts)
                if len(parts) == 7:
                    if parts[0] == "ok":
                        values = [float(x) for x in parts[1:]]
                        return (len(values), values)
                if len(parts) == 6:
                    if parts[0] == "ok":
                        return (0, None)
                return (0, None)
            except Exception as e:
                try:
                    self.log_error(f"Error parsing ok data: {e}")
                    return (0, None)
                finally:
                    e = None
                    del e

        if not isinstance(data, str):
            self.log_error(f"Data is not a string: {type(data)}")
            return
        try:
            self.log_message(f"Received: {data}")
            if data.startswith("ok"):
                len_, values = parse_ok_data(data)
                if len_ == 1:
                    return
                if len_ == 0:
                    return
                if self.is_need_Lpos:
                    x, y, z, rx, ry, rz = values
                    self.curreentLpos = [x, y, z, rx, ry, rz]
                    self.is_need_Lpos = False
                    if hasattr(self, "position_update_callback"):
                        callback = self.position_update_callback
                        delattr(self, "position_update_callback")
                        callback()
                elif self.is_recording:
                    x, y, z, rx, ry, rz = values
                    point = Point(x, y, z, rx, ry, rz, self.hand_state)
                    self.log_message(f"录制坐标: ({x}, {y}, {z}, {rx}, {ry}, {rz})")
                    self.action_manager_tab.coord_points_q.put(point)
                else:
                    if len_ == 6 and self.getJ == True:
                        self.handle_position_data(values, "J")
                        self.getJ = False
                    else:
                        if len_ == 6 and self.getL == True:
                            self.handle_position_data(values, "L")
                            self.getL = False
                        else:
                            if len_ == 6 and self.current_request == "GETJPOS":
                                self.on_execute_action(values)
                            else:
                                if len_ == 6 and self.current_request == "GETLPOS":
                                    self.on_execute_action(values)
                                else:
                                    self.log_error(f"Unresolved command: {self.current_request}")
        except Exception as e:
            try:
                self.log_error(f"Error logging data: {e}")
            finally:
                e = None
                del e

    def handle_position_data(self, data, label_prefix):
        try:
            pos_str = ",".join(((f"{x:.2f}") for x in data))
            if label_prefix == "J":
                self.label_6.setText(f"{pos_str}")
            else:
                if label_prefix == "L":
                    self.label_7.setText(f"{pos_str}")
        except Exception as e:
            try:
                self.log_error(f"Error handling {label_prefix}POS: {e}")
            finally:
                e = None
                del e

    def refresh_timer(self):
        while True:
            time.sleep(3)
            self.refresh_serial_ports()

    def __init__(self):
        super().__init__()
        self.setupUi(self)
        self.rootpath = "config/"
        self.txtSend.addItems(self.load_history())
        timer_thread = Thread(target=(self.refresh_timer), daemon=True)
        timer_thread.start()
        self.serial = None
        self.receiver_thread = None
        self.dummy_init()
        self.init_var()
        self.ui_init()
        self.move_init()
        self.sub_ui_init()
        self.config = configparser.ConfigParser()
        self.default_config = {"UI": {
                'title': '"Dummy_小宅测试版"', 
                'show_about': True, 
                'Teaching_time_interval_ms': '"200"', 
                '注意': '"WorldOrJoint变更（J/L）前请删除历史示教记录，否则运行示教会发生碰撞！！"', 
                'WorldOrJoint': '"J"', 
                'Foolproof': True}}
        CONFIG_FILE = self.rootpath + "settings.ini"
        if not os.path.exists(CONFIG_FILE):
            self.config.read_dict(self.default_config)
            with open(CONFIG_FILE, "w") as f:
                self.config.write(f)
        else:
            self.config.read(CONFIG_FILE)
        self.setWindowTitle(self.config.get("UI", "title", fallback="Dummy_小宅测试版"))
        self.update_ui_from_config()
        self.Foolproof = self.config.getboolean("UI", "Foolproof", fallback=True)
        import itertools
        self.cmd_iterator = itertools.cycle([f"#CMDMODE {i}" for i in (0, 1, 2, 3)])

    def init_xbox(self):

        def button_pressed():
            print("[回调] 按钮被按下!")
            self.index_hand_command(HandCommand.CLOSE)

        def button_released():
            print("[回调] 按钮被释放!")
            self.index_hand_command(HandCommand.OPEN)
            time.sleep(1)
            self.index_hand_command(HandCommand.DISABLE)

        def axis_moved(value):
            print(f"[回调] 摇杆移动: {value:.2f}")

        def hat_moved(value):
            print(f"[回调] 轨迹球移动: {value}")
            x, y = value
            if x == -1:
                if hasattr(self, "pushButton_X_2"):
                    self.pushButton_X_2.click()
            elif x == 1:
                if hasattr(self, "pushButton_X_2"):
                    self.pushButton_X.click()
            elif y == -1:
                if hasattr(self, "pushButton_Y_"):
                    self.pushButton_Y_.click()
            elif y == 1:
                if hasattr(self, "pushButton_Y"):
                    self.pushButton_Y.click()

        def hello():
            controller = ThreadedGamepadController()
            controller.register_button_callback(0, on_press=button_pressed, on_release=button_released)
            controller.register_axis_callback(0, axis_moved)
            controller.register_hat_callback(0, hat_moved)
            controller.start()

    def update_ui_from_config(self):
        """根据配置文件更新UI显示"""
        if self.config.has_section("UI"):
            show_about = self.config.getboolean("UI", "show_about", fallback=True)
            index = self.tabWidget.indexOf(self.tab_2)
            self.tabWidget.setTabVisible(index, show_about)
            send_interval = self.config.getint("UI", "Teaching_time_interval_ms", fallback=200)
            if send_interval >= 50:
                self.Teaching_time_interval = send_interval
            JL = self.config.get("UI", "WorldOrJoint", fallback="J")
            if JL == "J" or JL == "L":
                self.WorldOrJoint = SYMBOL_MAP[JL]

    def init_var(self):
        self.current_request = None
        self.getJ = False
        self.getL = False
        self.is_recording = None
        self.is_need_Lpos = True
        self.curreentLpos = [0, 0, 0, 0, 0, 0]
        self.last_send_time = 0
        self.Teaching_time_interval = 200
        self.WorldOrJoint = SYMBOL_MAP["J"]
        self.hand_state = 0

    def sub_ui_init(self):
        self.action_manager = ActionManager(self.rootpath + "actions.json")
        self.action_manager_tab = ActionManagerWindow(self.action_manager)
        self.tabWidget.insertTab(2, self.action_manager_tab, "示教")
        self.action_manager_tab.signals.start_recording.connect(self.on_start_recording)
        self.action_manager_tab.signals.stop_recording.connect(self.on_stop_recording)
        self.action_manager_tab.signals.execute_action.connect(self.on_execute_action)
        self.action_manager_tab.signals.stop_action.connect(self.on_stop_action)
        self.action_manager_tab.signals.stop_action.connect(self.on_stop_action)
        self.action_manager_tab.setEnabled(False)
        self.activation_manager = ActivationManager()
        self.activation_ui = ActivationApp(self.activation_manager)
        acted = self.activation_ui.activation_manager.check_activation()
        if acted:
            print("软件已激活！")
            self.action_manager_tab.setEnabled(True)
        else:
            print("测试模式。")
            self.action_manager_tab.setEnabled(False)
        self.btn_act.clicked.connect(self.btn_activatAPP)

    def on_start_recording(self):
        """开始录制槽函数"""
        self.is_recording = True
        self.recorded_points = []
        self.log_message("开始录制动作...")
        self.log_message("关闭电机使能...")
        self.uni_send_data("!DISABLED")
        self.record_timer = QTimer(self)
        self.record_timer.timeout.connect(self.request_current_position)
        self.record_timer.start(self.Teaching_time_interval)

    def on_stop_recording(self):
        """停止录制槽函数"""
        self.is_recording = False
        self.record_timer.stop()
        self.log_message("开启电机使能...")
        self.uni_send_data("!START")

    def request_current_position(self, L_J=None):
        """向串口发送请求，获取当前坐标"""
        if self.serial:
            if self.serial.is_open:
                if L_J is None:
                    if self.WorldOrJoint == SYMBOL_MAP["L"]:
                        self.uni_send_data("#GETLPOS")
                    else:
                        self.uni_send_data("#GETJPOS")
                else:
                    if L_J == "J":
                        self.uni_send_data("#GETJPOS")
                    else:
                        self.uni_send_data("#GETLPOS")

    def index_hand_command(self, cmd):
        """简化版夹爪控制"""
        if isinstance(cmd, HandCommand):
            self.uni_send_data(cmd.value)
        else:
            if cmd in (0, 1, 2, 3):
                state = list(HandCommand)[cmd]
                self.uni_send_data(state.value)
            else:
                self.log_message("err hand command")

    def on_execute_action(self, points: List[Point]):
        """执行动作槽函数，使用多线程避免阻塞UI"""

        def execute_points():
            self.log_message("开始执行动作...")
            if self.action_manager_tab.ui.checkBox_loop.isChecked():
                while not self.stop_flag:
                    self.log_message("循环动作执行开始！")
                    for point in points:
                        if self.stop_flag:
                            self.log_message("动作执行中断！")
                            break
                        command = f"{self.WorldOrJoint}{point.x}, {point.y}, {point.z}, {point.rx}, {point.ry}, {point.rz}"
                        self.uni_send_data(command)
                        self.log_message(f"执行坐标: {command}")
                        if point.h != self.hand_state:
                            self.index_hand_command(point.h)
                            self.hand_state = point.h
                        time.sleep(0.001 * self.Teaching_time_interval)

            else:
                for point in points:
                    if self.stop_flag:
                        self.log_message("动作执行中断！")
                        break
                    command = f"{self.WorldOrJoint}{point.x}, {point.y}, {point.z}, {point.rx}, {point.ry}, {point.rz}"
                    self.uni_send_data(command)
                    self.log_message(f"执行坐标: {command}")
                    if point.h != self.hand_state:
                        self.index_hand_command(point.h)
                        self.hand_state = point.h
                    time.sleep(0.001 * self.Teaching_time_interval)

            self.is_running = False
            self.stop_flag = False
            self.action_manager_tab.is_running = False
            self.action_manager_tab.execute_button.setText("执行")
            self.log_message("动作执行完毕！")

        if self.is_running:
            self.stop_flag = True
            self.log_message("正在停止动作...")
        else:
            self.stop_flag = False
            self.is_running = True
            self.thread = threading.Thread(target=execute_points)
            self.thread.start()

    def on_stop_action(self):
        self.log_message("正在停止动作...")
        self.stop_flag = True

    def ui_init(self):
        self.cboxPortName = self.findChild(QComboBox, "cboxPortName")
        self.cboxBaudRate = self.findChild(QComboBox, "cboxBaudRate")
        self.cboxDataBit = self.findChild(QComboBox, "cboxDataBit")
        self.cboxParity = self.findChild(QComboBox, "cboxParity")
        self.cboxStopBit = self.findChild(QComboBox, "cboxStopBit")
        self.btnOpen = self.findChild(QPushButton, "btnOpen")
        self.txtDataHex = self.findChild(QTextEdit, "txtDataHex")
        self.txtSend = self.findChild(QComboBox, "txtSend")
        self.btnSend = self.findChild(QPushButton, "btnSend")
        self.ckHexSend = self.findChild(QCheckBox, "ckHexSend")
        self.ckHexReceive = self.findChild(QCheckBox, "ckHexReceive")
        self.labIsOpen = self.findChild(QLabel, "labIsOpen")
        self.labSend = self.findChild(QLabel, "labSend")
        self.labReceive = self.findChild(QLabel, "labReceive")
        self.refresh_serial_ports()
        self.cboxBaudRate.addItems(['9600', '19200', '38400', '57600', '115200'])
        self.cboxDataBit.addItems(["8", "7", "6", "5"])
        self.cboxParity.addItems(['None', 'Odd', 'Even', 'Mark', 'Space'])
        self.cboxStopBit.addItems(["1", "1.5", "2"])
        self.btnOpen.clicked.connect(self.toggle_serial)
        self.btnSend.clicked.connect(self.btn_send_data)
        self.btnClearAll.clicked.connect(self.btn_clear_data)
        self.cboxBaudRate.setCurrentIndex(4)
        self.ckHexSend.setChecked(False)
        self.ckHexReceive.setChecked(False)
        pixmap = QPixmap(":/imgs/ico/wx.jpg")
        self.label_img.setPixmap(pixmap)
        self.setWindowIcon(QIcon(":/imgs/ico/ico.png"))
        self.thread = None
        self.is_running = False
        self.stop_flag = False
        self.logger = Logger(self.txtDataHex)

    def move_init(self):
        self.pushButton_Reset = self.findChild(QPushButton, "pushButton_reset")
        self.pushButton_Reset.clicked.connect(self.on_reset_button_clicked)
        self.horizontalSlider1 = self.findChild(QSlider, "horizontalSlider1")
        self.horizontalSlider_2 = self.findChild(QSlider, "horizontalSlider_2")
        self.horizontalSlider_3 = self.findChild(QSlider, "horizontalSlider_3")
        self.horizontalSlider_4 = self.findChild(QSlider, "horizontalSlider_4")
        self.horizontalSlider_6 = self.findChild(QSlider, "horizontalSlider_6")
        self.horizontalSlider_7 = self.findChild(QSlider, "horizontalSlider_7")
        self.horizontalSlider1.valueChanged.connect(lambda value: self.on_joint_slider_changed(0, value))
        self.horizontalSlider_2.valueChanged.connect(lambda value: self.on_joint_slider_changed(1, value))
        self.horizontalSlider_3.valueChanged.connect(lambda value: self.on_joint_slider_changed(2, value))
        self.horizontalSlider_4.valueChanged.connect(lambda value: self.on_joint_slider_changed(3, value))
        self.horizontalSlider_6.valueChanged.connect(lambda value: self.on_joint_slider_changed(4, value))
        self.horizontalSlider_7.valueChanged.connect(lambda value: self.on_joint_slider_changed(5, value))
        self.pushButton_X = self.findChild(QPushButton, "pushButton_X")
        self.pushButton_X_2 = self.findChild(QPushButton, "pushButton_X_2")
        self.pushButton_Y = self.findChild(QPushButton, "pushButton_Y")
        self.pushButton_Y_ = self.findChild(QPushButton, "pushButton_Y_")
        self.pushButton_Z = self.findChild(QPushButton, "pushButton_Z")
        self.pushButton_Z_ = self.findChild(QPushButton, "pushButton_Z_")
        self.pushButton_P_2 = self.findChild(QPushButton, "pushButton_P_2")
        self.pushButton_X.clicked.connect(lambda: self.on_world_button_clicked("X+"))
        self.pushButton_X_2.clicked.connect(lambda: self.on_world_button_clicked("X-"))
        self.pushButton_Y.clicked.connect(lambda: self.on_world_button_clicked("Y+"))
        self.pushButton_Y_.clicked.connect(lambda: self.on_world_button_clicked("Y-"))
        self.pushButton_Z.clicked.connect(lambda: self.on_world_button_clicked("Z+"))
        self.pushButton_Z_.clicked.connect(lambda: self.on_world_button_clicked("Z-"))
        self.pushButton_P_2.clicked.connect(lambda: self.on_world_button_clicked("PAUSE"))
        self.pushButton_X = self.findChild(QPushButton, "pushButton_XX")
        self.pushButton_X_2 = self.findChild(QPushButton, "pushButton_XX_")
        self.pushButton_Y = self.findChild(QPushButton, "pushButton_YY")
        self.pushButton_Y_ = self.findChild(QPushButton, "pushButton_YY_")
        self.pushButton_Z = self.findChild(QPushButton, "pushButton_ZZ")
        self.pushButton_Z_ = self.findChild(QPushButton, "pushButton_ZZ_")
        self.pushButton_X.clicked.connect(lambda: self.on_world_button_clicked("RX+"))
        self.pushButton_X_2.clicked.connect(lambda: self.on_world_button_clicked("RX-"))
        self.pushButton_Y.clicked.connect(lambda: self.on_world_button_clicked("RY+"))
        self.pushButton_Y_.clicked.connect(lambda: self.on_world_button_clicked("RY-"))
        self.pushButton_Z.clicked.connect(lambda: self.on_world_button_clicked("RZ+"))
        self.pushButton_Z_.clicked.connect(lambda: self.on_world_button_clicked("RZ-"))

    def dummy_init(self):
        self.can_click = True
        self.global_cooldowns = {
         'STOP': 0.1, 
         'START': 0.1, 
         'HOME': 8, 
         'CALOBRATION': 8, 
         'RESET': 8, 
         'DISABLE': 0.1}
        self.pushButton.clicked.connect(self.dummy_button_action)
        self.pushButton_2.clicked.connect(self.dummy_button_action)
        self.pushButton_3.clicked.connect(self.dummy_button_action)
        self.pushButton_4.clicked.connect(self.dummy_button_action)
        self.pushButton_5.clicked.connect(self.dummy_button_action)
        self.pushButton_6.clicked.connect(self.dummy_button_action)
        self.pushButton_open.setShortcut("Q")
        self.pushButton_close.setShortcut("W")
        self.pushButton_ENA_DIS.setShortcut("E")
        self.pushButton_open.clicked.connect(self.dummy_button_hand)
        self.pushButton_close.clicked.connect(self.dummy_button_hand)
        self.pushButton_ENA_DIS.clicked.connect(self.dummy_button_hand)
        self.pushButton_hand_offset.clicked.connect(self.dummy_button_handoffset)
        self.horizontalSlider_angle.valueChanged.connect(self.on_slider_changed)
        self.horizontalSlider_current.valueChanged.connect(self.on_slider_changed)
        self.pushButton_P.clicked.connect(self.dummy_set_PID)
        self.pushButton_I.clicked.connect(self.dummy_set_PID)
        self.pushButton_D.clicked.connect(self.dummy_set_PID)
        self.pushButton_REBOOT.clicked.connect(self.dummy_REBOOT_J)
        self.pushButton_CMD.clicked.connect(self.dummy_set_PID)
        self.pushButton_V.clicked.connect(self.dummy_set_PID)
        self.pushButton_ACC.clicked.connect(self.dummy_Other)
        self.pushButton_OFFSET.clicked.connect(self.dummy_Other)
        self.pushButton_SPEED.clicked.connect(self.dummy_Other)
        self.pushButton_I_LIMIT.clicked.connect(self.dummy_Other)
        self.pushButton_endp1.clicked.connect(self.dummy_move)
        self.pushButton_endp2.clicked.connect(self.dummy_move)
        self.pushButton_joint.clicked.connect(self.dummy_move)
        self.pushButton_GL.clicked.connect(self.dummy_move)
        self.pushButton_GJ.clicked.connect(self.dummy_move)
        self.indexs = [
         'A', 'B', 'C', 'D', 'E', 'F', 'H']
        self.buttontexts = self.loadButtonTexts()
        for i, index in enumerate(self.indexs):
            if i in self.buttontexts:
                self.comboBox.setItemText(i, index + ":" + self.buttontexts[i])

        self.pushButton_7.clicked.connect(self.SAVE_changed)
        self.pushButton_A.setShortcut("1")
        self.pushButton_B.setShortcut("2")
        self.pushButton_C.setShortcut("3")
        self.pushButton_DD.setShortcut("4")
        self.pushButton_E.setShortcut("5")
        self.pushButton_F.setShortcut("6")
        self.pushButton_G.setShortcut("7")
        self.pushButton_A.clicked.connect(self.dummy_button_history_pos)
        self.pushButton_B.clicked.connect(self.dummy_button_history_pos)
        self.pushButton_C.clicked.connect(self.dummy_button_history_pos)
        self.pushButton_DD.clicked.connect(self.dummy_button_history_pos)
        self.pushButton_E.clicked.connect(self.dummy_button_history_pos)
        self.pushButton_F.clicked.connect(self.dummy_button_history_pos)
        self.pushButton_G.clicked.connect(self.dummy_button_history_pos)

    def uni_send_data(self, data, CRLF=True):
        """
        独立出来的发送数据方法
        :param data: 要发送的数据（字符串）
        :param CRLF: 是否在数据末尾添加 CRLF
        :return: True 表示发送成功，False 表示发送失败
        """
        try:
            if self.serial is None or not self.serial.is_open:
                self.log_error("Serial port is not open")
                return False
            if CRLF:
                data += "\r\n"
            data_bytes = data.encode()
            self.serial.write(data_bytes)
            self.log_message(f"Send: {data.strip()}")
            if self.txtSend.findText(data.strip()) == -1:
                self.txtSend.addItem(data.strip())
            return True
        except Exception as e:
            try:
                self.log_error(f"Error sending data: {e}")
                return False
            finally:
                e = None
                del e

    def dummy_move(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        data = None
        button = self.sender()
        if button == self.pushButton_endp1:
            data = "@" + self.spinBox_1_1.text().strip() + "," + self.spinBox_1_2.text().strip() + "," + self.spinBox_1_3.text().strip() + "," + self.spinBox_1_4.text().strip() + "," + self.spinBox_1_5.text().strip() + "," + self.spinBox_1_6.text().strip()
        else:
            if button == self.pushButton_endp2:
                data = "@" + self.spinBox_2_1.text().strip() + "," + self.spinBox_2_2.text().strip() + "," + self.spinBox_2_3.text().strip() + "," + self.spinBox_2_4.text().strip() + "," + self.spinBox_2_5.text().strip() + "," + self.spinBox_2_6.text().strip()
            else:
                if button == self.pushButton_joint:
                    data = "&" + self.doubleSpinBox_9_1.text().strip() + "," + self.doubleSpinBox_9_2.text().strip() + "," + self.doubleSpinBox_9_3.text().strip() + "," + self.doubleSpinBox_9_4.text().strip() + "," + self.doubleSpinBox_9_5.text().strip() + "," + self.doubleSpinBox_9_6.text().strip()
                else:
                    if button == self.pushButton_GL:
                        data = "#GETLPOS"
                        self.getL = True
                    else:
                        if button == self.pushButton_GJ:
                            data = "#GETJPOS"
                            self.getJ = True
                        else:
                            print("Button not open")
        if data != None:
            self.uni_send_data(data)

    def dummy_set_PID(self):
        """
        根据 comboBox 的文本名称和 doubleSpinBox_P 的值发送 #SET_DCE_KP 命令
        """
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        button = self.sender()
        name = button.text().strip()
        if name == "CMDMODE":
            try:
                command = next(self.cmd_iterator)
                self.uni_send_data(command)
            except StopIteration:
                pass

            return
        end_of_string = name[-1:]
        node, kp_value = (1, 200)
        if end_of_string == "P":
            jname = self.comboBox_PID.currentText().strip()
            ji = jname[-1:]
            node = int(ji)
            kp_value = self.doubleSpinBox_P.value()
        elif end_of_string == "I":
            jname = self.comboBox_PID.currentText().strip()
            ji = jname[-1:]
            node = int(ji)
            kp_value = self.doubleSpinBox_I.value()
        else:
            if end_of_string == "D":
                jname = self.comboBox_PID.currentText().strip()
                ji = jname[-1:]
                node = int(ji)
                kp_value = self.doubleSpinBox_D.value()
            else:
                if end_of_string == "V":
                    jname = self.comboBox_PID.currentText().strip()
                    ji = jname[-1:]
                    node = int(ji)
                    kp_value = self.doubleSpinBox_V.value()
        try:
            if node < 1 or node > 7:
                raise ValueError("Node must be between 1 and 7")
            command = f"#SET_DCE_K{end_of_string} {node} {kp_value}"
            self.log_message(f"Generated command: {command}")
            self.uni_send_data(command)
        except Exception as e:
            try:
                self.log_error(f"Unexpected error: {e}")
            finally:
                e = None
                del e

    def dummy_REBOOT_J(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        try:
            jname = self.comboBox_R.currentText().strip()
            ji = jname[-1:]
            node = int(ji)
            if node < 1 or node > 7:
                raise ValueError("Node must be between 1 and 7")
            command = f"#REBOOT {node}"
            self.log_message(f"Generated command: {command}")
            self.uni_send_data(command)
        except Exception as e:
            try:
                self.log_error(f"Unexpected error: {e}")
            finally:
                e = None
                del e

    def dummy_Other(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        try:
            jname = self.comboBox_R.currentText().strip()
            ji = jname[-1:]
            value = self.doubleSpinBox_SPEED_ACC.value()
            node = int(ji)
            if node < 1 or node > 7:
                raise ValueError("Node must be between 1 and 7")
            else:
                button = self.sender()
                cmd = button.text().strip()
                if cmd == "OFFSET_J":
                    command = f"#{cmd} {node}"
                else:
                    if cmd == "I_LIMIT_J":
                        lim = min(2.0, value)
                        command = f"#{cmd} {node} {lim}"
                    else:
                        command = f"#{cmd} {node} {value}"
            self.log_message(f"Generated command: {command}")
            self.uni_send_data(command)
        except Exception as e:
            try:
                self.log_error(f"Unexpected error: {e}")
            finally:
                e = None
                del e

    def SAVE_changed(self):
        self.getJ = True
        self.uni_send_data("#GETJPOS")
        time.sleep(0.05)
        QApplication.processEvents()
        self.getJ = False
        index = self.comboBox.currentIndex()
        self.comboBox.setItemText(index, self.indexs[index] + ":" + self.label_6.text())
        self.buttontexts[index] = self.label_6.text()

    def dummy_button_history_pos(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        button = self.sender()
        if button:
            text = button.text().strip()
            start = text[:1]
            i = self.indexs.index(start)
            text = self.buttontexts.get(i, "No text saved!")
            data = "&" + text.strip()
            self.uni_send_data(data)
        else:
            self.log_error("Button not found")

    def on_slider_changed(self, value):
        """滑块值改变时的槽函数"""
        current_time = time.time()
        if current_time - self.last_send_time <= 0.005:
            return
        slider = self.sender()
        if slider == self.horizontalSlider_current:
            cur = 0.1 * value
            self.label_current.setText(f"{cur:.1f}")
            data = "!" + f"HAND_I {cur:.1f}"
        else:
            self.label_pos.setText(f"{value}")
            data = "!" + f"HAND_POS {value}"
        self.uni_send_data(data)
        self.last_send_time = current_time

    def dummy_button_action(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        button = self.sender()
        if button:
            data = "!" + button.text().strip()
            self.uni_send_data(data)
            text = button.text().strip()
            self.can_click = False
            QTimer.singleShot(self.global_cooldowns.get(text, 0) * 1000, self._reset_cooldown)
        else:
            self.log_error("Button not found")

    def dummy_button_hand(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        button = self.sender()
        if button:
            if button == self.pushButton_open:
                self.index_hand_command(HandCommand.OPEN)
                self.hand_state = 2
            else:
                if button == self.pushButton_close:
                    self.index_hand_command(HandCommand.CLOSE)
                    self.hand_state = 3
                else:
                    if button == self.pushButton_ENA_DIS:
                        if self.hand_state == 0:
                            self.index_hand_command(HandCommand.ENABLE)
                            self.hand_state = 1
                        else:
                            self.index_hand_command(HandCommand.DISABLE)
                            self.hand_state = 0
                    else:
                        data = "!" + button.text().strip()
                        self.uni_send_data(data)
            text = button.text().strip()
            self.can_click = False
            QTimer.singleShot(self.global_cooldowns.get(text, 0) * 1000, self._reset_cooldown)
        else:
            self.log_error("Button not found")

    def dummy_button_handoffset(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        button = self.sender()
        if button:
            data = "!HAND_ZERO"
            self.uni_send_data(data)
        else:
            self.log_error("Button not found")

    def _reset_cooldown(self):
        """冷却结束后自动调用"""
        self.can_click = True

    def on_reset_button_clicked(self):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        self.horizontalSlider1.setValue(0)
        self.horizontalSlider_2.setValue(0)
        self.horizontalSlider_3.setValue(90)
        self.horizontalSlider_4.setValue(0)
        self.horizontalSlider_6.setValue(0)
        self.horizontalSlider_7.setValue(0)
        self.on_joint_slider_changed(0, 0, False)

    def on_joint_slider_changed(self, joint_index, value, protect=True):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        current_time = time.time()
        if protect:
            if current_time - self.last_send_time <= 0.01:
                return
        slider_values = [
         self.horizontalSlider1.value(),
         self.horizontalSlider_2.value(),
         self.horizontalSlider_3.value(),
         self.horizontalSlider_4.value(),
         self.horizontalSlider_6.value(),
         self.horizontalSlider_7.value()]
        data = "&" + ",".join(map(str, slider_values))
        self.uni_send_data(data)
        self.last_send_time = current_time
        print(f"关节 {joint_index + 1} 值改变: {data}")

    def on_world_button_clicked_old(self, action):
        """世界坐标按钮点击事件（支持线性/旋转轴，统一使用combox缩放）
        Args:
            action (str): 按钮名称，如 'X+', 'RY-', 'PAUSE' 等
        """
        print(f"世界坐标按钮点击: {action}")
        current_pos = []
        if self.curreentLpos != None:
            current_pos = self.curreentLpos
        else:
            self.current_position = [
             0, 0, 0, 0, 0, 0]
        if action == "PAUSE":
            print("暂停操作")
            return
        else:
            try:
                scale_text = self.comboBox_scale.currentText()
                scale_value = float(scale_text[-2:])
            except Exception as e:
                try:
                    print(f"比例获取错误: {e}, 使用默认值1.0")
                    scale_value = 1.0
                finally:
                    e = None
                    del e

            axis_mapping = {'X+':[
              0, 1], 
             'X-':[0, -1],  'Y+':[
              1, 1], 
             'Y-':[1, -1],  'Z+':[
              2, 1], 
             'Z-':[2, -1],  'RX+':[
              3, 1], 
             'RX-':[3, -1],  'RY+':[
              4, 1], 
             'RY-':[4, -1],  'RZ+':[
              5, 1], 
             'RZ-':[5, -1]}
            if action in axis_mapping:
                axis_idx, direction = axis_mapping[action]
                current_pos[axis_idx] += direction * scale_value
                if axis_idx >= 3:
                    current_pos[axis_idx] %= 360
                print(f"新坐标: X={current_pos[0]:.2f}, Y={current_pos[1]:.2f}, Z={current_pos[2]:.2f}, RX={current_pos[3]:.1f}°, RY={current_pos[4]:.1f}°, RZ={current_pos[5]:.1f}°")
                data = {
                 self.WorldOrJoint} + ",".join(map(str, current_pos))
                self.uni_send_data(data)
                time.sleep(1)
                self.is_need_Lpos = True
                self.request_current_position()
            else:
                print(f"错误: 未知按钮操作 {action}")

    def on_world_button_clicked(self, action):
        if self.can_click == False:
            if self.Foolproof == True:
                self.log_error("防呆模式已启动，稍后再试！")
                return
        now = time.time()
        if now - getattr(self, "last_click_time", 0) < 0.3:
            print("操作太频繁，请稍后再试")
            return
        self.last_click_time = now
        print(f"世界坐标按钮点击: {action}")
        if action == "PAUSE":
            print("暂停操作 功能待开发")
            return
        self.is_waiting_for_position = True
        self.position_update_callback = lambda: self._process_movement_after_position(action)
        self.is_need_Lpos = True
        self.request_current_position("l")

    def _process_movement_after_position(self, action):
        """获取到当前位置后处理移动逻辑"""
        self.is_waiting_for_position = False
        current_pos = self.curreentLpos if self.curreentLpos is not None else [0] * 6
        try:
            scale_text = self.comboBox_scale.currentText()
            scale_value = float(scale_text[-2:]) / 1.0
        except Exception as e:
            try:
                print(f"比例获取错误: {e}, 使用默认值1.0")
                scale_value = 1.0
            finally:
                e = None
                del e

        axis_mapping = {'X+':[
          0, 1], 
         'X-':[0, -1],  'Y+':[
          1, 1], 
         'Y-':[1, -1],  'Z+':[
          2, 1], 
         'Z-':[2, -1],  'RX+':[
          3, 1], 
         'RX-':[3, -1],  'RY+':[
          4, 1], 
         'RY-':[4, -1],  'RZ+':[
          5, 1], 
         'RZ-':[5, -1]}
        if action in axis_mapping:
            axis_idx, direction = axis_mapping[action]
            current_pos[axis_idx] += direction * scale_value
            if axis_idx >= 3:
                current_pos[axis_idx] %= 360
                if current_pos[axis_idx] >= 180:
                    current_pos[axis_idx] -= 360
            print(f"发送新坐标: X={current_pos[0]:.2f}, Y={current_pos[1]:.2f}, Z={current_pos[2]:.2f}, RX={current_pos[3]:.1f}°, RY={current_pos[4]:.1f}°, RZ={current_pos[5]:.1f}°")
            data = "@" + ",".join(map(str, current_pos))
            self.uni_send_data(data)

    def refresh_serial_ports(self):
        if self.btnOpen.text() == "Close":
            return
        else:
            new_ports = self.serial_ports_plus()
            if sys.platform.startswith("win"):
                new_ports = sorted(new_ports, key=(lambda p: int(p[3:])), reverse=True)
            else:
                new_ports = sorted(new_ports)
        current_ports = [self.cboxPortName.itemText(i) for i in range(self.cboxPortName.count())]
        if new_ports == current_ports:
            return
        self.cboxPortName.clear()
        for port in new_ports:
            self.cboxPortName.addItem(port, port)

    def serial_ports_plus(self):
        """获取系统可用串口列表"""
        if sys.platform.startswith("win"):
            ports = [f"COM{i}" for i in range(1, 256)]
        else:
            if sys.platform.startswith("linux") or sys.platform.startswith("cygwin"):
                ports = glob.glob("/dev/tty[A-Za-z]*")
            else:
                if sys.platform.startswith("darwin"):
                    ports = glob.glob("/dev/tty.*")
                else:
                    raise EnvironmentError("Unsupported platform")
        available_ports = []
        for port in ports:
            try:
                with serial.Serial(port, timeout=0.02, write_timeout=0.02) as s:
                    s.port = port
                    time.sleep(0.01)
            except (OSError, serial.SerialException, ValueError):
                continue
            else:
                available_ports.append(port)

        return available_ports

    def info_error(self, error_message):
        logging.error(error_message)
        QMessageBox.critical(self, "Error", error_message)

    def toggle_serial(self):
        if self.serial and self.serial.is_open:
            self.close_serial()
        else:
            self.open_serial()

    def open_serial(self):
        port = self.cboxPortName.currentText()
        baud_rate = int(self.cboxBaudRate.currentText())
        data_bits = int(self.cboxDataBit.currentText())
        parity = self.get_parity(self.cboxParity.currentText())
        stop_bits = float(self.cboxStopBit.currentText())
        try:
            self.serial = serial.Serial(port,
              baud_rate, bytesize=data_bits, parity=parity,
              stopbits=stop_bits,
              write_timeout=0.2)
            self.receiver_thread = SerialReceiver(self.serial)
            self.receiver_thread.data_received.connect(self.append_received_data)
            self.receiver_thread.start()
            self.btnOpen.setText("Close")
            self.labIsOpen.setText(f"串口状态: 打开 {port}")
            self.gbox1.setDisabled(True)
        except serial.SerialException as e:
            try:
                self.info_error(f"Failed to open serial port: {e}")
            finally:
                e = None
                del e

    def close_serial(self):
        if self.receiver_thread:
            self.receiver_thread.stop()
            self.receiver_thread.wait()
            self.receiver_thread = None
        if self.serial:
            self.serial.close()
            self.serial = None
        self.btnOpen.setText("Open")
        self.labIsOpen.setText("串口状态: 关闭")
        self.gbox1.setDisabled(False)

    def get_parity(self, parity_str):
        return {'None':serial.PARITY_NONE, 
         'Odd':serial.PARITY_ODD, 
         'Even':serial.PARITY_EVEN, 
         'Mark':serial.PARITY_MARK, 
         'Space':serial.PARITY_SPACE}[parity_str]

    def btn_clear_data(self):
        self.txtDataHex.clear()

    def btn_send_data(self):
        data = self.txtSend.currentText().strip()
        self.uni_send_data(data)

    def btn_activatAPP(self):
        """打开激活界面"""
        if self.activation_ui.activation_manager.check_activation():
            QMessageBox.information(self, "提示", "已成功激活，无需重复操作！")
        else:
            self.activation_ui.show()

    def log_message_old(self, message):
        current_time = datetime.datetime.now().strftime("%H:%M:%S")
        log_message = f"[{current_time}] {message}"
        self.txtDataHex.append(log_message)

    def log_error_old(self, error_message):
        current_time = datetime.datetime.now().strftime("%H:%M:%S")
        error_log = f"[{current_time}] ERROR:{error_message}"
        self.txtDataHex.append(error_log)

    def log_message(self, message):
        log_message = f"LOG:{message}"
        self.logger.log(log_message)

    def log_error(self, error_message):
        error_log = f"ERR:{error_message}"
        self.logger.err(error_log)

    def loadButtonTexts(self):
        if os.path.exists(self.rootpath + "ABC_texts.txt"):
            with open(self.rootpath + "ABC_texts.txt", "r") as file:
                texts = {}
                for line in file:
                    parts = line.strip().split(": ")
                    if len(parts) == 2:
                        texts[int(parts[0])] = parts[1]

                return texts
        else:
            return {}

    def closeEvent(self, event):
        with open(self.rootpath + "ABC_texts.txt", "w") as file:
            for index, text in self.buttontexts.items():
                file.write(f"{index}: {text}\n")

        self.close_serial()
        self.save_history()
        super().closeEvent(event)

    def load_history(self, max_lines=200):
        if os.path.exists(self.rootpath + "history.txt"):
            with open((self.rootpath + "history.txt"), "r", encoding="utf-8") as file:
                return [line.strip() for line in file.readlines()[-max_lines:]]
        return []

    def save_history(self, max_lines=500):
        try:
            items = []
            for i in range(self.txtSend.count()):
                items.append(self.txtSend.itemText(i))

            if len(items) > max_lines:
                items = items[-max_lines:]
            with open((self.rootpath + "history.txt"), "w", encoding="utf-8") as file:
                for item in items:
                    file.write(item + "\n")

            self.log_message(f"Saved last {len(items)} lines to history.txt")
        except Exception as e:
            try:
                self.log_error(f"Error saving history: {e}")
            finally:
                e = None
                del e


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())
