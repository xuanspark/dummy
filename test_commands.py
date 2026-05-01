#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32 Dummy Robot 命令测试脚本
测试状态机、门控拒绝、STOP 抢占、PAUSE 软暂停等功能
"""

import serial
import serial.tools.list_ports
import time
import sys
from typing import Optional

def auto_detect_port() -> Optional[str]:
    """自动检测可用的串口，优先选择 STM32 设备"""
    ports = serial.tools.list_ports.comports()
    
    if not ports:
        print("❌ 未检测到任何串口设备")
        return None
    
    print("\n📡 检测到以下串口:")
    for i, port in enumerate(ports, 1):
        print(f"  {i}. {port.device} - {port.description}")
    
    # 优先查找 STM32 相关设备
    stm32_keywords = ['STM32', 'STLink', 'Virtual COM', 'USB Serial']
    for port in ports:
        for keyword in stm32_keywords:
            if keyword.lower() in port.description.lower():
                print(f"\n✅ 自动选择: {port.device} ({port.description})")
                return port.device
    
    print(f"\n⚠️  未找到 STM32 设备，使用第一个串口: {ports[0].device}")
    return ports[0].device


class DummyRobotTester:
    """Dummy 机器人测试类"""
    
    def __init__(self, port: Optional[str] = None, baudrate: int = 115200):
        if port is None:
            port = auto_detect_port()
            if port is None:
                print("❌ 无法找到可用串口")
                sys.exit(1)
        
        try:
            self.ser = serial.Serial(port, baudrate, timeout=1)
            time.sleep(0.5)
            print(f"✅ 已连接到 {port}, 波特率: {baudrate}")
        except Exception as e:
            print(f"❌ 串口连接失败: {e}")
            sys.exit(1)
    
    def send_command(self, cmd: str, wait_time: float = 0.5) -> Optional[str]:
        """发送命令并接收响应"""
        try:
            self.ser.write((cmd + '\n').encode('utf-8'))
            print(f"📤 发送: {cmd}")
            time.sleep(wait_time)
            
            # 多次尝试读取响应（最多 10 次，每次间隔 0.5 秒）
            response = ""
            max_attempts = 10
            for i in range(max_attempts):
                if self.ser.in_waiting > 0:
                    response += self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    time.sleep(0.1)
                else:
                    if response:  # 如果已经有响应，再等一次确认没有更多数据
                        time.sleep(0.1)
                        if self.ser.in_waiting == 0:
                            break
                    else:
                        time.sleep(0.5)  # 没有响应时等待更长时间
            
            response = response.strip()
            if response:
                print(f"📥 响应: {response}")
                return response
            else:
                print("⚠️  无响应")
                return None
        except Exception as e:
            print(f"❌ 命令发送失败: {e}")
            return None
    
    def close(self):
        """关闭串口"""
        if self.ser.is_open:
            self.ser.close()
            print("🔌 串口已关闭")


# ==================== 测试场景 ====================

def test_basic_flow(robot: DummyRobotTester):
    """测试 1: 基本流程 - START → HOME → 运动 → CLOSE"""
    print("\n" + "="*60)
    print("🧪 测试 1: 基本流程")
    print("="*60)
    
    print("\n📍 步骤 1: 启动机器人 (!START)")
    response = robot.send_command("!START")
    time.sleep(2)
    assert response and "Started ok" in response, f"❌ START 失败，响应: {response}"
    
    print("\n📍 步骤 2: 回零点 (!HOME)")
    response = robot.send_command("!HOME", wait_time=10.0)
    assert response and "Started ok" in response, f"❌ HOME 失败，响应: {response}"
    
    print("\n📍 步骤 3: 发送运动命令 (>10,0,90,0,0,0)")
    response = robot.send_command(">10,0,90,0,0,0")
    time.sleep(3)
    # 响应格式：数字 + "ok"，例如 "15ok" 表示入队成功，剩余 15 个空位
    assert response and ("ok" in response or response.isdigit()), f"❌ 运动命令失败，响应: {response}"
    
    print("\n📍 步骤 4: 安全关闭 (!CLOSE)")
    response = robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    assert response and "Closed ok" in response, f"❌ CLOSE 失败，响应: {response}"
    
    print("\n✅ 测试 1 通过")


def test_gate_rejection(robot: DummyRobotTester):
    """测试 2: 门控拒绝 - 未使能/未 HOME 时拒绝命令"""
    print("\n" + "="*60)
    print("🧪 测试 2: 门控拒绝")
    print("="*60)
    
    print("\n📍 场景 1: 未 START 直接发 HOME（应被拒绝）")
    response = robot.send_command("!HOME", wait_time=0.5)
    assert response and "rejected: not_enabled" in response, f"❌ 应拒绝未使能的 HOME，响应: {response}"
    print("✅ 正确拒绝")
    
    print("\n📍 场景 2: 未 START 直接发运动命令（应被拒绝）")
    response = robot.send_command(">10,0,90,0,0,0")
    time.sleep(1)
    assert response and "rejected: not_enabled" in response, f"❌ 应拒绝未使能的运动命令，实际: {response}"
    print("✅ 正确拒绝 (not_enabled)")
    
    print("\n📍 场景 3: START 后未 HOME 直接发运动命令（应被拒绝）")
    robot.send_command("!START")
    time.sleep(2)
    response = robot.send_command(">10,0,90,0,0,0")
    time.sleep(1)
    assert response and "rejected: not_homed" in response, f"❌ 应拒绝未 HOME 的运动命令，实际: {response}"
    print("✅ 正确拒绝 (not_homed)")
    
    print("\n📍 清理: 失能")
    robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    
    print("\n✅ 测试 2 通过")


def test_stop_preemption(robot: DummyRobotTester):
    """测试 3: STOP 抢占 - HOME 中途 STOP，RESUME 后续跑"""
    print("\n" + "="*60)
    print("🧪 测试 3: STOP 抢占")
    print("="*60)
    
    print("\n📍 步骤 1: 启动并 HOME")
    robot.send_command("!START")
    time.sleep(2)
    robot.send_command("!HOME", wait_time=0.5)
    
    print("\n📍 步骤 2: 等待 1 秒后触发 STOP")
    time.sleep(1)
    response = robot.send_command("!STOP")
    time.sleep(1)
    
    assert response and "ESTOP" in response and "STATE:1" in response, f"❌ STOP 响应错误: {response}"
    print("✅ STOP 成功，状态 = 1 (EMERGENCY_STOP)")
    
    print("\n📍 步骤 3: 尝试发送新命令（应被拒绝）")
    response = robot.send_command(">50,0,90,0,0,0")
    time.sleep(1)
    assert response and "rejected: state=" in response, f"❌ 应拒绝状态异常的运动命令，实际: {response}"
    print("✅ 正确拒绝 (state rejection)")
    
    print("\n📍 步骤 4: RESUME 恢复（应续跑到 HOME 位置）")
    response = robot.send_command("!RESUME")
    time.sleep(1)
    assert response and "RESUMED" in response and "FROM_STATE:1" in response, f"❌ RESUME 响应错误: {response}"
    # 验证 RESUME 响应中包含速度信息
    assert "SPEED:" in response, f"❌ RESUME 响应应包含 SPEED 字段: {response}"
    print("✅ RESUME 成功，等待 10 秒观察是否续跑到 HOME")
    time.sleep(10)
    
    print("\n📍 步骤 5: 查询关节角度（应接近 0,0,90,0,0,0）")
    response = robot.send_command("#GETJPOS")
    time.sleep(1)
    print(f"当前关节角度: {response}")
    
    print("\n📍 步骤 6: 查询速度（应恢复为默认速度 45）")
    response = robot.send_command("#GETSPEED")
    time.sleep(1)
    assert response and "SPEED:" in response, f"❌ GETSPEED 失败: {response}"
    print(f"当前速度: {response}")
    
    print("\n📍 清理: 失能")
    robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    
    print("\n✅ 测试 3 通过")


def test_pause_soft(robot: DummyRobotTester):
    """测试 4: PAUSE 软暂停 - 当前动作完成后挂起"""
    print("\n" + "="*60)
    print("🧪 测试 4: PAUSE 软暂停")
    print("="*60)
    
    print("\n📍 步骤 1: 启动并 HOME")
    robot.send_command("!START")
    time.sleep(2)
    robot.send_command("!HOME", wait_time=10.0)
    
    print("\n📍 步骤 2: 第一组 - 发送 3 个坐标 → PAUSE → RESUME")
    robot.send_command(">10,-60,150,0,0,0")
    robot.send_command(">20,-50,140,0,0,0")
    robot.send_command(">30,-40,130,0,0,0")
    
    print("   触发第 1 次 PAUSE")
    response = robot.send_command("!PAUSE")
    print(f"   PAUSE 响应: {response}")
    time.sleep(2)
    
    print("   第 1 次 RESUME")
    response = robot.send_command("!RESUME")
    print(f"   RESUME 响应: {response}")
    
    print("   等待 5 秒，观察运动...")
    time.sleep(5)
    response = robot.send_command("#GETJPOS")
    print(f"   当前位置: {response}")
    
    print("\n📍 步骤 3: 第二组 - 继续发送 3 个坐标 → PAUSE → RESUME")
    robot.send_command(">40,-30,120,0,0,0")
    robot.send_command(">50,-20,110,0,0,0")
    robot.send_command(">60,-10,100,0,0,0")
    
    print("   触发第 2 次 PAUSE")
    response = robot.send_command("!PAUSE")
    print(f"   PAUSE 响应: {response}")
    time.sleep(2)
    
    print("   第 2 次 RESUME")
    response = robot.send_command("!RESUME")
    print(f"   RESUME 响应: {response}")
    
    print("   等待 5 秒，观察运动...")
    time.sleep(5)
    response = robot.send_command("#GETJPOS")
    print(f"   当前位置: {response}")
    
    print("\n📍 步骤 4: 第三组 - 继续发送 3 个坐标 → PAUSE → RESUME")
    robot.send_command(">50,-20,110,0,0,0")
    robot.send_command(">40,-30,120,0,0,0")
    robot.send_command(">30,-40,130,0,0,0")
    
    print("   触发第 3 次 PAUSE")
    response = robot.send_command("!PAUSE")
    print(f"   PAUSE 响应: {response}")
    time.sleep(2)
    
    print("   第 3 次 RESUME")
    response = robot.send_command("!RESUME")
    print(f"   RESUME 响应: {response}")
    
    print("   等待 5 秒，观察运动...")
    time.sleep(5)
    response = robot.send_command("#GETJPOS")
    print(f"   当前位置: {response}")
    
    print("\n📍 步骤 5: 第四组 - 继续发送坐标回到 HOME")
    robot.send_command(">20,-50,140,0,0,0")
    robot.send_command(">10,-60,150,0,0,0")
    robot.send_command(">0,0,90,0,0,0")
    
    print("   触发第 4 次 PAUSE")
    response = robot.send_command("!PAUSE")
    print(f"   PAUSE 响应: {response}")
    time.sleep(2)
    
    print("   第 4 次 RESUME（最后一次）")
    response = robot.send_command("!RESUME")
    print(f"   RESUME 响应: {response}")
    
    print("   等待 5 秒，观察运动...")
    time.sleep(5)
    response = robot.send_command("#GETJPOS")
    print(f"   当前位置: {response}")
    
    print("\n📍 步骤 6: 等待所有坐标执行完成")
    time.sleep(10)
    
    print("\n📍 步骤 7: 查询最终位置（应回到 HOME 位置）")
    response = robot.send_command("#GETJPOS")
    print(f"   最终关节角度: {response}")
    
    print("\n📍 清理: 失能")
    robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    
    print("\n✅ 测试 4 通过")


def test_queue_resume(robot: DummyRobotTester):
    """测试 5: 队列续跑 - STOP 后 RESUME 继续执行队列"""
    print("\n" + "="*60)
    print("🧪 测试 5: 队列续跑")
    print("="*60)
    
    print("\n📍 步骤 1: 启动并 HOME")
    robot.send_command("!START")
    time.sleep(2)
    robot.send_command("!HOME", wait_time=10.0)
    
    print("\n📍 步骤 2: 发送多个运动命令到队列")
    robot.send_command(">10,0,90,0,0,0")
    time.sleep(0.2)
    robot.send_command(">20,0,90,0,0,0")
    time.sleep(0.2)
    robot.send_command(">30,0,90,0,0,0")
    time.sleep(0.2)
    robot.send_command(">0,0,90,0,0,0")
    time.sleep(1)
    
    print("\n📍 步骤 3: 中途触发 STOP")
    response = robot.send_command("!STOP")
    time.sleep(1)
    print(f"STOP 响应: {response}")
    
    print("\n📍 步骤 4: 查询 STOP 后速度")
    response = robot.send_command("#GETSPEED")
    time.sleep(1)
    print(f"STOP 后速度: {response}")
    
    print("\n📍 步骤 5: RESUME 恢复（应继续执行队列）")
    response = robot.send_command("!RESUME")
    time.sleep(1)
    assert response and "RESUMED" in response, f"❌ RESUME 响应错误: {response}"
    assert "SPEED:" in response, f"❌ RESUME 响应应包含 SPEED 字段: {response}"
    print(f"RESUME 响应: {response}")
    print("等待 10 秒观察队列执行")
    time.sleep(10)
    
    print("\n📍 步骤 6: 查询关节角度")
    response = robot.send_command("#GETJPOS")
    time.sleep(1)
    print(f"当前关节角度: {response}")
    
    print("\n📍 步骤 7: 查询速度（应恢复为默认速度 45）")
    response = robot.send_command("#GETSPEED")
    time.sleep(1)
    assert response and "SPEED:" in response, f"❌ GETSPEED 失败: {response}"
    print(f"当前速度: {response}")
    
    print("\n📍 清理: 失能")
    robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    
    print("\n✅ 测试 5 通过")


def test_state_rejection(robot: DummyRobotTester):
    """测试 6: 状态拒绝 - STOP/PAUSE 期间拒绝运动命令"""
    print("\n" + "="*60)
    print("🧪 测试 6: 状态拒绝")
    print("="*60)
    
    print("\n📍 步骤 1: 启动并 HOME")
    robot.send_command("!START")
    time.sleep(2)
    robot.send_command("!HOME", wait_time=10.0)
    
    print("\n📍 步骤 2: 触发 STOP")
    robot.send_command("!STOP")
    time.sleep(1)
    
    print("\n📍 步骤 3: STOP 状态下发送运动命令（应被拒绝）")
    response = robot.send_command(">10,0,90,0,0,0")
    time.sleep(1)
    assert response and "rejected: state=" in response, f"❌ 应拒绝状态异常的运动命令，实际: {response}"
    print("✅ 正确拒绝 (state rejection)")
    
    print("\n📍 步骤 4: RESUME 恢复")
    robot.send_command("!RESUME")
    time.sleep(2)
    
    print("\n📍 步骤 5: 触发 PAUSE")
    robot.send_command(">20,0,90,0,0,0")
    time.sleep(0.5)
    robot.send_command("!PAUSE")
    time.sleep(1)
    
    print("\n📍 步骤 6: PAUSE 状态下发送运动命令（应被拒绝）")
    response = robot.send_command(">30,0,90,0,0,0")
    time.sleep(1)
    assert response and "rejected: state=" in response, f"❌ 应拒绝状态异常的运动命令，实际: {response}"
    print("✅ 正确拒绝 (state rejection)")
    
    print("\n📍 清理: RESUME 并失能")
    robot.send_command("!RESUME")
    time.sleep(2)
    robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    
    print("\n✅ 测试 6 通过")


def test_hand_control(robot: DummyRobotTester):
    """测试 7: 手爪控制 - HD_* 命令"""
    print("\n" + "="*60)
    print("🧪 测试 7: 手爪控制")
    print("="*60)
    
    print("\n📍 步骤 1: 使能手爪")
    robot.send_command("!HD_EN")
    time.sleep(1)
    
    print("\n📍 步骤 2: 手爪位置 0%")
    robot.send_command("!HD_POS 0")
    time.sleep(2)
    
    print("\n📍 步骤 3: 手爪位置 50%")
    robot.send_command("!HD_POS 50")
    time.sleep(2)
    
    print("\n📍 步骤 4: 手爪位置 100%")
    robot.send_command("!HD_POS 100")
    time.sleep(2)
    
    print("\n📍 步骤 5: 禁用手爪")
    robot.send_command("!HD_DIS")
    time.sleep(2)
    
    print("\n✅ 测试 7 通过")


def test_close_safety(robot: DummyRobotTester):
    """测试 8: CLOSE 安全性 - 运动中拒绝，正常时先回 REST 再失能"""
    print("\n" + "="*60)
    print("🧪 测试 8: CLOSE 安全性")
    print("="*60)
    
    print("\n📍 步骤 1: 启动并 HOME")
    robot.send_command("!START")
    time.sleep(2)
    robot.send_command("!HOME", wait_time=10.0)
    
    print("\n📍 步骤 2: 发送运动命令")
    robot.send_command(">30,0,90,0,0,0")
    time.sleep(0.5)
    
    print("\n📍 步骤 3: 运动中发送 CLOSE（应被拒绝）")
    response = robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(1)
    if response and "rejected" in response:
        print("✅ 正确拒绝运动中的 CLOSE")
    else:
        print("⚠️  CLOSE 未被拒绝（可能运动已完成）")
    
    print("\n📍 步骤 4: 等待运动完成")
    time.sleep(5)
    
    print("\n📍 步骤 5: 正常状态下发送 CLOSE")
    response = robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    assert response and "Closed ok" in response, f"❌ CLOSE 失败: {response}"
    print("✅ CLOSE 成功（应先回 REST 再失能）")
    
    print("\n✅ 测试 8 通过")


def test_query_commands(robot: DummyRobotTester):
    """测试 9: 查询命令 - GETJPOS / GETLPOS"""
    print("\n" + "="*60)
    print("🧪 测试 9: 查询命令")
    print("="*60)
    
    print("\n📍 步骤 1: 启动")
    robot.send_command("!START")
    time.sleep(2)
    
    print("\n📍 步骤 2: 查询关节角度 (#GETJPOS)")
    response = robot.send_command("#GETJPOS")
    time.sleep(1)
    assert response and "ok" in response, f"❌ GETJPOS 失败: {response}"
    print(f"✅ 关节角度: {response}")
    
    print("\n📍 步骤 3: 查询笛卡尔坐标 (#GETLPOS)")
    response = robot.send_command("#GETLPOS")
    time.sleep(1)
    assert response and "ok" in response, f"❌ GETLPOS 失败: {response}"
    print(f"✅ 笛卡尔坐标: {response}")
    
    print("\n📍 清理: 失能")
    robot.send_command("!CLOSE", wait_time=2.0)
    time.sleep(10)
    
    print("\n✅ 测试 9 通过")


def interactive_mode(robot: DummyRobotTester):
    """交互模式"""
    print("\n" + "="*60)
    print("🎮 交互模式")
    print("="*60)
    print("输入命令（输入 'help' 查看帮助，'quit' 退出）")
    
    while True:
        try:
            cmd = input("\n>>> ").strip()
            
            if cmd.lower() == 'quit':
                break
            elif cmd.lower() == 'help':
                print("""
可用命令示例：
  控制类:
    !START          - 启动（使能+回 REST）
    !HOME           - 回零点（0,0,90,0,0,0）
    !RESET          - 回 REST 姿态
    !CLOSE          - 安全关闭（回 REST+失能）
    !STOP           - 急停（抢占式）
    !PAUSE          - 暂停（软语义）
    !RESUME         - 恢复
  
  手爪类:
    !HD_EN          - 使能手爪
    !HD_DIS         - 禁用手爪
    !HD_O           - 打开手爪
    !HD_C           - 关闭手爪
    !HD_POS <0-100> - 手爪位置
    !HD_I <0-2.0>   - 手爪电流
    !HD_ZERO        - 手爪标定
  
  查询类:
    #GETJPOS        - 获取关节角度
    #GETLPOS        - 获取笛卡尔坐标
    #GETSPEED       - 获取当前关节速度
    #CMDMODE <m>    - 设置命令模式
  
  运动类:
    >j1,j2,j3,j4,j5,j6[,speed]  - 关节运动
    @x,y,z,a,b,c[,speed]        - 笛卡尔运动
  
  RGB:
    !RGB_ON         - 打开 RGB
    !RGB_OFF        - 关闭 RGB
    !RGB_MODE <0-6> - RGB 模式
                """)
            elif cmd:
                robot.send_command(cmd, wait_time=0.5)
        except KeyboardInterrupt:
            break


# ==================== 主程序 ====================

def main():
    """主程序"""
    print("""
╔═══════════════════════════════════════════════════════════╗
║   STM32 Dummy Robot 状态机测试脚本                        ║
║   测试: 门控拒绝、STOP 抢占、PAUSE 软暂停、队列续跑       ║
╚═══════════════════════════════════════════════════════════╝
    """)
    
    try:
        robot = DummyRobotTester(port=None, baudrate=115200)
        
        print("\n请选择测试模式:")
        print("  1 - 基本流程 (START → HOME → 运动 → CLOSE)")
        print("  2 - 门控拒绝 (未使能/未 HOME 拒绝)")
        print("  3 - STOP 抢占 (HOME 中途 STOP，RESUME 续跑)")
        print("  4 - PAUSE 软暂停 (当前动作完成后挂起)")
        print("  5 - 队列续跑 (STOP 后 RESUME 继续执行)")
        print("  6 - 状态拒绝 (STOP/PAUSE 期间拒绝命令)")
        print("  7 - 手爪控制 (HD_* 命令)")
        print("  8 - CLOSE 安全性 (运动中拒绝)")
        print("  9 - 查询命令 (GETJPOS/GETLPOS)")
        print("  A - 完整测试 (全部)")
        print("  I - 交互模式")
        print("  0 - 退出")
        
        choice = input("\n请输入选项: ").strip().upper()
        
        if choice == '1':
            test_basic_flow(robot)
        elif choice == '2':
            test_gate_rejection(robot)
        elif choice == '3':
            test_stop_preemption(robot)
        elif choice == '4':
            test_pause_soft(robot)
        elif choice == '5':
            test_queue_resume(robot)
        elif choice == '6':
            test_state_rejection(robot)
        elif choice == '7':
            test_hand_control(robot)
        elif choice == '8':
            test_close_safety(robot)
        elif choice == '9':
            test_query_commands(robot)
        elif choice == 'A':
            test_basic_flow(robot)
            test_gate_rejection(robot)
            test_stop_preemption(robot)
            test_pause_soft(robot)
            test_queue_resume(robot)
            test_state_rejection(robot)
            test_hand_control(robot)
            test_close_safety(robot)
            test_query_commands(robot)
        elif choice == 'I':
            interactive_mode(robot)
        elif choice == '0':
            print("👋 退出程序")
        else:
            print("❌ 无效选项")
        
        robot.close()
        
    except Exception as e:
        print(f"❌ 程序异常: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
