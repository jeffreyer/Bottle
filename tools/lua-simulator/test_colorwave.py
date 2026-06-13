#!/usr/bin/env python3
"""
测试脚本：彩色波浪动画
演示如何用Python编写类似Lua的动画效果
"""

import sys
import os

# 添加当前目录到路径
sys.path.insert(0, os.path.dirname(__file__))

from simulator import LuaSimulator, SimulatorGUI
import math

# 创建模拟器（32x8）
simulator = LuaSimulator(width=32, height=8)

# 配置
move_interval = 30

# 定义setup函数
def setup():
    print("ColorWave loaded")
    print(f"Screen size: {simulator.width}x{simulator.height}")
    simulator.led_api.clear()
    simulator.led_api.show()

# 定义loop函数
def loop():
    t = simulator.time_api.millis() / move_interval

    # 使用WIDTH和HEIGHT全局变量
    for x in range(simulator.width):
        for y in range(simulator.height):
            # 调整色相计算以适应更宽的屏幕
            hue = int((x * 8 + y * 10 + t) % 255)
            r, g, b = simulator.led_api.hsv(hue, 255, 255)
            simulator.led_api.set(x, y, r, g, b)

    simulator.led_api.show()
    # time.delay(30) - 在模拟器中忽略

# 定义unload函数
def unload():
    print("ColorWave unloaded")
    simulator.led_api.clear()
    simulator.led_api.show()

# 注册函数
simulator.setup_func = setup
simulator.loop_func = loop
simulator.unload_func = unload

# 运行GUI
if __name__ == '__main__':
    print("=" * 50)
    print("ESP32 Lua Simulator - 彩色波浪测试")
    print("=" * 50)
    print()
    print("这是一个测试脚本，演示Python模拟Lua动画")
    print()
    print("控制:")
    print("  空格 - 暂停/恢复")
    print("  R - 重新加载")
    print("  ESC - 退出")
    print()
    print("=" * 50)

    gui = SimulatorGUI(simulator)
    gui.run()
