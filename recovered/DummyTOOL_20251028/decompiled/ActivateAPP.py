# uncompyle6 version 3.9.3
# Python bytecode version base 3.7.0 (3394)
# Decompiled from: Python 3.13.13 (tags/v3.13.13:01104ce, Apr  7 2026, 19:25:48) [MSC v.1944 64 bit (AMD64)]
# Embedded file name: ActivateAPP.py
import sys
from PyQt5.QtWidgets import QApplication, QWidget, QLabel, QLineEdit, QPushButton, QVBoxLayout, QMessageBox
from PyQt5.QtCore import Qt
import hashlib, wmi, os

class ActivationManager:

    def __init__(self):
        self.hardware_id = self.get_hardware_id()
        self.salt = "Code6317".encode()

    def get_hardware_id(self):
        """获取设备序列号"""
        c = wmi.WMI()
        for bios in c.Win32_ComputerSystemProduct():
            return bios.UUID

        return

    def generate_authorization_code(self):
        """生成授权码"""
        if self.hardware_id:
            return hashlib.md5(self.hardware_id.encode() + self.salt).hexdigest()
        return

    def check_activation(self):
        """检查是否已激活"""
        try:
            with open("activation.key", "r") as f:
                saved_code = f.read().strip()
                current_code = self.generate_authorization_code()
                return saved_code == current_code
        except FileNotFoundError:
            return False

    def activate(self, activation_code):
        """激活软件"""
        current_code = self.generate_authorization_code()
        if activation_code == current_code:
            with open("activation.key", "w") as f:
                f.write(current_code)
            return True
        return False


class ActivationApp(QWidget):

    def __init__(self, activation_manager):
        super().__init__()
        self.activation_manager = activation_manager
        self.initUI()

    def initUI(self):
        self.setWindowTitle("软件激活")
        frameGeometry = self.frameGeometry()
        centerPoint = QApplication.desktop().screen().rect().center()
        frameGeometry.moveCenter(centerPoint)
        self.move(frameGeometry.topLeft())
        self.resize(800, 600)
        layout = QVBoxLayout()
        self.serial_label = QLabel(f"设备序列号: \n{self.activation_manager.hardware_id}")
        layout.addWidget(self.serial_label)
        self.serial_label.setTextInteractionFlags(Qt.TextSelectableByMouse | Qt.TextSelectableByKeyboard)
        self.copy_serial_button = QPushButton("复制序列号")
        self.copy_serial_button.clicked.connect(self.copy_serial_to_clipboard)
        layout.addWidget(self.copy_serial_button)
        self.activation_label = QLabel("请输入授权码:")
        layout.addWidget(self.activation_label)
        self.activation_code_input = QLineEdit()
        layout.addWidget(self.activation_code_input)
        self.activate_button = QPushButton("激活")
        self.activate_button.clicked.connect(self.activate)
        layout.addWidget(self.activate_button)
        self.setLayout(layout)

    def copy_serial_to_clipboard(self):
        """将设备序列号复制到剪贴板"""
        clipboard = QApplication.clipboard()
        clipboard.setText(self.activation_manager.hardware_id)
        QMessageBox.information(self, "复制成功", "设备序列号已复制到剪贴板。")

    def generate_authorization_code(self):
        """生成授权码并显示在可复制的消息框中"""
        authorization_code = self.activation_manager.generate_authorization_code()
        if authorization_code:
            msg_box = QMessageBox(self)
            msg_box.setWindowTitle("授权码")
            msg_box.setText(f"您的授权码是:\n{authorization_code}")
            msg_box.setTextInteractionFlags(Qt.TextSelectableByMouse | Qt.TextSelectableByKeyboard)
            msg_box.setStandardButtons(QMessageBox.Ok)
            msg_box.exec_()
        else:
            QMessageBox.warning(self, "错误", "无法获取设备序列号！")

    def activate(self):
        """激活软件"""
        activation_code = self.activation_code_input.text()
        if self.activation_manager.activate(activation_code):
            self.show_activated_message()
        else:
            QMessageBox.warning(self, "激活失败", "测试中，暂未开放，敬请期待！")

    def show_activated_message(self):
        """显示激活成功消息"""
        QMessageBox.information(self, "激活成功", "软件已成功激活,重启后生效！")
        self.close()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    activation_manager = ActivationManager()
    ex = ActivationApp(activation_manager)
    ex.show()
    sys.exit(app.exec_())
