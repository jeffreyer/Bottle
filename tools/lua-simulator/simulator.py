#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 Lua LED Matrix Simulator
模拟ESP32设备上的Lua动画效果，提高开发效率
"""

import pygame
import math
import time
import json
import sys
import os
from pathlib import Path
import colorsys

# 配置
MATRIX_WIDTH = 32
MATRIX_HEIGHT = 8
PIXEL_SIZE = 20
PIXEL_GAP = 2
WINDOW_PADDING = 50
FPS = 60

# 颜色
BG_COLOR = (20, 20, 20)
PIXEL_OFF_COLOR = (10, 10, 10)

class LEDMatrix:
    """LED矩阵显示"""

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.buffer = [[(0, 0, 0) for _ in range(width)] for _ in range(height)]
        self.dirty = True

    def set(self, x, y, r, g, b):
        """设置单个像素"""
        x = int(x)
        y = int(y)
        if 0 <= x < self.width and 0 <= y < self.height:
            self.buffer[y][x] = (int(r), int(g), int(b))
            self.dirty = True

    def clear(self):
        """清空显示"""
        self.buffer = [[(0, 0, 0) for _ in range(self.width)] for _ in range(self.height)]
        self.dirty = True

    def show(self):
        """刷新显示（在这里只是标记为脏）"""
        self.dirty = True

    def get_pixel(self, x, y):
        """获取像素颜色"""
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.buffer[y][x]
        return (0, 0, 0)

class LuaLEDAPI:
    """Lua LED API实现"""

    def __init__(self, matrix):
        self.matrix = matrix

        # 完整的3x5字体（与设备一致）- ASCII字符（space到z）
        # 每个字符3列宽，5行高
        # Bit 4 = 顶行, Bit 0 = 底行
        # 所有值只使用位0-4（0x00-0x1F范围）
        self.font_3x5 = {
            ' ': [0x00, 0x00, 0x00],  # 0x20 space
            '!': [0x00, 0x17, 0x00],  # 0x21 !
            '"': [0x03, 0x00, 0x03],  # 0x22 "
            '#': [0x0A, 0x1F, 0x0A],  # 0x23 #
            '$': [0x12, 0x1F, 0x09],  # 0x24 $
            '%': [0x13, 0x04, 0x19],  # 0x25 %
            '&': [0x0A, 0x15, 0x0A],  # 0x26 &
            "'": [0x00, 0x03, 0x00],  # 0x27 '
            '(': [0x00, 0x0E, 0x11],  # 0x28 (
            ')': [0x11, 0x0E, 0x00],  # 0x29 )
            '*': [0x0A, 0x04, 0x0A],  # 0x2A *
            '+': [0x04, 0x0E, 0x04],  # 0x2B +
            ',': [0x00, 0x18, 0x00],  # 0x2C ,
            '-': [0x04, 0x04, 0x04],  # 0x2D -
            '.': [0x00, 0x10, 0x00],  # 0x2E .
            '/': [0x18, 0x04, 0x03],  # 0x2F /
            '0': [0x1F, 0x11, 0x1F],  # 0x30 0
            '1': [0x00, 0x1F, 0x00],  # 0x31 1
            '2': [0x1D, 0x15, 0x17],  # 0x32 2
            '3': [0x11, 0x15, 0x1F],  # 0x33 3
            '4': [0x07, 0x04, 0x1F],  # 0x34 4
            '5': [0x17, 0x15, 0x1D],  # 0x35 5
            '6': [0x1F, 0x15, 0x1D],  # 0x36 6
            '7': [0x01, 0x01, 0x1F],  # 0x37 7
            '8': [0x1F, 0x15, 0x1F],  # 0x38 8
            '9': [0x17, 0x15, 0x1F],  # 0x39 9
            ':': [0x00, 0x0A, 0x00],  # 0x3A :
            ';': [0x00, 0x1A, 0x00],  # 0x3B ;
            '<': [0x04, 0x0A, 0x11],  # 0x3C <
            '=': [0x0A, 0x0A, 0x0A],  # 0x3D =
            '>': [0x11, 0x0A, 0x04],  # 0x3E >
            '?': [0x01, 0x15, 0x02],  # 0x3F ?
            '@': [0x0E, 0x15, 0x16],  # 0x40 @
            'A': [0x1E, 0x05, 0x1E],  # 0x41 A
            'B': [0x1F, 0x15, 0x0A],  # 0x42 B
            'C': [0x0E, 0x11, 0x11],  # 0x43 C
            'D': [0x1F, 0x11, 0x0E],  # 0x44 D
            'E': [0x1F, 0x15, 0x11],  # 0x45 E
            'F': [0x1F, 0x05, 0x01],  # 0x46 F
            'G': [0x0E, 0x11, 0x1D],  # 0x47 G
            'H': [0x1F, 0x04, 0x1F],  # 0x48 H
            'I': [0x11, 0x1F, 0x11],  # 0x49 I
            'J': [0x08, 0x10, 0x0F],  # 0x4A J
            'K': [0x1F, 0x04, 0x1B],  # 0x4B K
            'L': [0x1F, 0x10, 0x10],  # 0x4C L
            'M': [0x1F, 0x02, 0x1F],  # 0x4D M
            'N': [0x1F, 0x01, 0x1F],  # 0x4E N
            'O': [0x0E, 0x11, 0x0E],  # 0x4F O
            'P': [0x1F, 0x05, 0x02],  # 0x50 P
            'Q': [0x0E, 0x19, 0x1E],  # 0x51 Q
            'R': [0x1F, 0x05, 0x1A],  # 0x52 R
            'S': [0x12, 0x15, 0x09],  # 0x53 S
            'T': [0x01, 0x1F, 0x01],  # 0x54 T
            'U': [0x0F, 0x10, 0x0F],  # 0x55 U
            'V': [0x07, 0x18, 0x07],  # 0x56 V
            'W': [0x1F, 0x08, 0x1F],  # 0x57 W
            'X': [0x1B, 0x04, 0x1B],  # 0x58 X
            'Y': [0x03, 0x1C, 0x03],  # 0x59 Y
            'Z': [0x19, 0x15, 0x13],  # 0x5A Z
            '[': [0x00, 0x1F, 0x11],  # 0x5B [
            '\\': [0x03, 0x04, 0x18], # 0x5C backslash
            ']': [0x11, 0x1F, 0x00],  # 0x5D ]
            '^': [0x02, 0x01, 0x02],  # 0x5E ^
            '_': [0x10, 0x10, 0x10],  # 0x5F _
            '`': [0x00, 0x01, 0x02],  # 0x60 `
            'a': [0x0C, 0x14, 0x1C],  # 0x61 a (lowercase, smaller)
            'b': [0x1F, 0x14, 0x08],  # 0x62 b
            'c': [0x08, 0x14, 0x14],  # 0x63 c (lowercase, smaller)
            'd': [0x08, 0x14, 0x1F],  # 0x64 d
            'e': [0x08, 0x14, 0x18],  # 0x65 e (lowercase, smaller)
            'f': [0x04, 0x1E, 0x05],  # 0x66 f
            'g': [0x08, 0x14, 0x1C],  # 0x67 g (lowercase, no descender)
            'h': [0x1F, 0x04, 0x18],  # 0x68 h
            'i': [0x00, 0x1D, 0x00],  # 0x69 i
            'j': [0x10, 0x1D, 0x00],  # 0x6A j (lowercase, no descender)
            'k': [0x1F, 0x08, 0x14],  # 0x6B k
            'l': [0x00, 0x1F, 0x10],  # 0x6C l
            'm': [0x1C, 0x04, 0x1C],  # 0x6D m (lowercase, smaller)
            'n': [0x1C, 0x04, 0x18],  # 0x6E n (lowercase, smaller)
            'o': [0x08, 0x14, 0x08],  # 0x6F o (lowercase, smaller)
            'p': [0x1C, 0x14, 0x08],  # 0x70 p (lowercase, no descender)
            'q': [0x08, 0x14, 0x1C],  # 0x71 q (lowercase, no descender)
            'r': [0x1C, 0x04, 0x04],  # 0x72 r (lowercase, smaller)
            's': [0x10, 0x14, 0x08],  # 0x73 s (lowercase, smaller)
            't': [0x04, 0x1E, 0x10],  # 0x74 t
            'u': [0x0C, 0x10, 0x1C],  # 0x75 u (lowercase, smaller)
            'v': [0x0C, 0x10, 0x0C],  # 0x76 v (lowercase, smaller)
            'w': [0x1C, 0x08, 0x1C],  # 0x77 w (lowercase, smaller)
            'x': [0x14, 0x08, 0x14],  # 0x78 x (lowercase, smaller)
            'y': [0x04, 0x18, 0x04],  # 0x79 y (lowercase, no descender)
            'z': [0x14, 0x1C, 0x14],  # 0x7A z (lowercase, smaller)
        }

    def set(self, x, y, r, g, b):
        """设置像素"""
        self.matrix.set(x, y, r, g, b)

    def clear(self):
        """清空"""
        self.matrix.clear()

    def show(self):
        """显示"""
        self.matrix.show()

    def hsv(self, h, s, v):
        """HSV转RGB"""
        h = (h % 255) / 255.0
        s = min(255, max(0, s)) / 255.0
        v = min(255, max(0, v)) / 255.0
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        return int(r * 255), int(g * 255), int(b * 255)

    def text(self, x, y, text, r, g, b):
        """绘制文本 - 使用与设备一致的3x5位图字体"""
        x_offset = 0
        for char in text:  # 不转大写，支持大小写
            if char in self.font_3x5:
                glyph = self.font_3x5[char]  # 3列数据

                # 绘制字符（3列宽，5行高）
                for col in range(3):
                    if x + x_offset + col >= self.matrix.width:
                        break

                    for row in range(5):
                        if y + row >= self.matrix.height:
                            continue

                        # 检查位图中的像素（从bit 4到bit 0读取）
                        # Bit 4 = 顶行, Bit 0 = 底行
                        pixel = (glyph[col] >> (4 - row)) & 0x01

                        if pixel and x + x_offset + col >= 0 and y + row >= 0:
                            self.set(x + x_offset + col, y + row, r, g, b)

                x_offset += 4  # 3像素字符 + 1像素间距
            else:
                # 未知字符，跳过
                x_offset += 4

    def palette(self, palette_table, index, brightness=255):
        """从调色板获取颜色"""
        index = index % 256
        brightness = min(255, max(0, brightness)) / 255.0

        # 查找索引两侧的颜色
        lower = None
        upper = None

        for entry in palette_table:
            pos = entry[0]
            if pos <= index:
                lower = entry
            if pos >= index and upper is None:
                upper = entry

        if lower is None:
            lower = palette_table[0]
        if upper is None:
            upper = palette_table[-1]

        # 线性插值
        if lower[0] == upper[0]:
            r, g, b = lower[1], lower[2], lower[3]
        else:
            t = (index - lower[0]) / (upper[0] - lower[0])
            r = int(lower[1] + (upper[1] - lower[1]) * t)
            g = int(lower[2] + (upper[2] - lower[2]) * t)
            b = int(lower[3] + (upper[3] - lower[3]) * t)

        # 应用亮度
        r = int(r * brightness)
        g = int(g * brightness)
        b = int(b * brightness)

        return r, g, b

class LuaFFTAPI:
    """模拟FFT API"""

    def __init__(self):
        self.data = [0.0] * 512
        self.time_offset = 0

    def get(self, index):
        """获取频段值（模拟数据）"""
        if 0 <= index < 512:
            # 模拟音频数据
            t = time.time() * 10 + self.time_offset
            value = abs(math.sin(t + index * 0.1)) * 5000 + abs(math.sin(t * 2 + index * 0.05)) * 3000
            return value
        return 0.0

    def count(self):
        """返回频段数量"""
        return 512

class LuaGravityAPI:
    """模拟重力感应API"""

    def __init__(self):
        self.gx = 0.0
        self.gy = 0.0
        self.gz = 1.0

    def get(self):
        """获取重力数据"""
        return self.gx, self.gy, self.gz, True

    def set(self, gx, gy, gz):
        """设置重力数据（用于键盘控制）"""
        self.gx = gx
        self.gy = gy
        self.gz = gz

class LuaButtonAPI:
    """模拟按键API"""

    def __init__(self):
        self.event = 0
        self.holding = False

    def poll(self):
        """获取按键事件"""
        event = self.event
        self.event = 0
        return event

    def is_holding(self):
        """是否按住"""
        return self.holding

    def trigger_click(self):
        """触发单击"""
        self.event = 1

    def trigger_long_press(self):
        """触发长按"""
        self.event = 2

    def set_holding(self, holding):
        """设置按住状态"""
        self.holding = holding

class LuaTimeAPI:
    """模拟时间API"""

    def __init__(self):
        self.start_time = time.time()
        self.delay_until = 0

    def millis(self):
        """返回毫秒数"""
        return int((time.time() - self.start_time) * 1000)

    def delay(self, ms):
        """延迟（在模拟器中不实际延迟）"""
        self.delay_until=time.time() + ms / 1000.0

    def now(self):
        """返回当前时间"""
        t = time.localtime()
        return {
            'year': t.tm_year,
            'month': t.tm_mon,
            'day': t.tm_mday,
            'hour': t.tm_hour,
            'min': t.tm_min,
            'sec': t.tm_sec
        }

class LuaSimulator:
    """Lua模拟器主类"""

    def __init__(self, width=MATRIX_WIDTH, height=MATRIX_HEIGHT):
        self.width = width
        self.height = height
        self.matrix = LEDMatrix(width, height)

        # API对象
        self.led_api = LuaLEDAPI(self.matrix)
        self.fft_api = LuaFFTAPI()
        self.gravity_api = LuaGravityAPI()
        self.button_api = LuaButtonAPI()
        self.time_api = LuaTimeAPI()

        # Lua环境（使用Python模拟）
        self.lua_globals = {
            'WIDTH': width,
            'HEIGHT': height,
            'CONFIG': {},
            'led': self.led_api,
            'fft': self.fft_api,
            'gravity': self.gravity_api,
            'button': self.button_api,
            'time': self.time_api,
            'math': math,
            'print': print,
            'use': lambda x: None,  # 资源声明（忽略）
        }

        self.lua_code = None
        self.setup_func = None
        self.loop_func = None
        self.unload_func = None

    def load_lua_file(self, filepath):
        """加载Lua文件"""
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                self.lua_code = f.read()

            # 简单的Python执行（实际应该用lua解释器）
            # 这里我们用Python语法模拟
            print(f"已加载: {filepath}")
            print("注意: 这是Python模拟器，不是真正的Lua解释器")
            print("要完整支持Lua，请安装lupa: pip install lupa")
            return True

        except Exception as e:
            print(f"加载失败: {e}")
            return False

    def load_config(self, config_file):
        """加载配置文件"""
        try:
            with open(config_file, 'r', encoding='utf-8') as f:
                config_data = json.load(f)

            # 转换配置为CONFIG表
            for item in config_data:
                key = item.get('key')
                default = item.get('default')
                if key and default is not None:
                    self.lua_globals['CONFIG'][key] = default

            print(f"已加载配置: {len(self.lua_globals['CONFIG'])} 项")
            return True

        except Exception as e:
            print(f"配置加载失败: {e}")
            return False

    def call_setup(self):
        """调用setup函数"""
        if self.setup_func:
            try:
                self.setup_func()
            except Exception as e:
                print(f"setup错误: {e}")

    def call_loop(self):
        """调用loop函数"""
        if self.loop_func:
            try:
                self.loop_func()
            except Exception as e:
                print(f"loop错误: {e}")

    def call_unload(self):
        """调用unload函数"""
        if self.unload_func:
            try:
                self.unload_func()
            except Exception as e:
                print(f"unload错误: {e}")

class SimulatorGUI:
    """模拟器GUI"""

    def __init__(self, simulator):
        self.simulator = simulator

        # 初始化Pygame
        pygame.init()

        # 计算窗口大小
        window_width = (PIXEL_SIZE + PIXEL_GAP) * simulator.width + WINDOW_PADDING * 2
        window_height = (PIXEL_SIZE + PIXEL_GAP) * simulator.height + WINDOW_PADDING * 2 + 100

        self.screen = pygame.display.set_mode((window_width, window_height))
        pygame.display.set_caption(f"ESP32 Lua Simulator - {simulator.width}x{simulator.height}")

        self.clock = pygame.time.Clock()
        self.running = True
        self.paused = False
        self.delayed = False

        # 字体
        self.font = pygame.font.Font(None, 24)

    def draw_matrix(self):
        """绘制LED矩阵"""
        for y in range(self.simulator.height):
            for x in range(self.simulator.width):
                # 计算屏幕位置
                # 注意：Lua坐标系是左下角为(0,0)，需要翻转Y轴
                screen_x = WINDOW_PADDING + x * (PIXEL_SIZE + PIXEL_GAP)
                screen_y = WINDOW_PADDING + (self.simulator.height - 1 - y) * (PIXEL_SIZE + PIXEL_GAP)

                # 获取像素颜色
                r, g, b = self.simulator.matrix.get_pixel(x, y)

                # 如果是关闭的，显示暗色
                if r == 0 and g == 0 and b == 0:
                    color = PIXEL_OFF_COLOR
                else:
                    color = (r, g, b)

                # 绘制像素
                pygame.draw.rect(self.screen, color,
                               (screen_x, screen_y, PIXEL_SIZE, PIXEL_SIZE))

    def draw_info(self):
        """绘制信息"""
        info_y = WINDOW_PADDING + (PIXEL_SIZE + PIXEL_GAP) * self.simulator.height + 20

        # FPS
        fps_text = self.font.render(f"FPS: {int(self.clock.get_fps())}", True, (255, 255, 255))
        self.screen.blit(fps_text, (WINDOW_PADDING, info_y))

        # 状态
        status = "PAUSED" if self.paused else "RUNNING"
        status_text = self.font.render(status, True, (0, 255, 0) if not self.paused else (255, 255, 0))
        self.screen.blit(status_text, (WINDOW_PADDING + 150, info_y))

        # 提示
        help_text = self.font.render("Space:Pause R:Reload Arrow:Gravity Mouse:Button ESC:Exit", True, (150, 150, 150))
        self.screen.blit(help_text, (WINDOW_PADDING, info_y + 30))

    def handle_events(self):
        """处理事件"""
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.running = False

            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    self.running = False
                elif event.key == pygame.K_SPACE:
                    self.paused = not self.paused
                elif event.key == pygame.K_r:
                    print("重载Lua脚本...")
                    self.simulator.call_setup()

                # 重力控制
                elif event.key == pygame.K_UP:
                    self.simulator.gravity_api.set(0.75, 0, 0.866)
                elif event.key == pygame.K_DOWN:
                    self.simulator.gravity_api.set(-0.75, 0, 0.866)
                elif event.key == pygame.K_LEFT:
                    self.simulator.gravity_api.set(0, -0.75, 0.866)
                elif event.key == pygame.K_RIGHT:
                    self.simulator.gravity_api.set(0, 0.75, 0.866)

            elif event.type == pygame.KEYUP:
                # 松开方向键，重力归零
                if event.key in [pygame.K_UP, pygame.K_DOWN, pygame.K_LEFT, pygame.K_RIGHT]:
                    self.simulator.gravity_api.set(0, 0, 1.0)

            elif event.type == pygame.MOUSEBUTTONDOWN:
                if event.button == 1:  # 左键
                    self.simulator.button_api.trigger_click()
                    self.simulator.button_api.set_holding(True)

            elif event.type == pygame.MOUSEBUTTONUP:
                if event.button == 1:
                    self.simulator.button_api.set_holding(False)

    def run(self):
        """运行模拟器"""
        # 调用setup
        self.simulator.call_setup()

        while self.running:
            self.handle_events()

            # 清屏
            self.screen.fill(BG_COLOR)

            # 如果没暂停，执行loop
            if not self.paused and not self.delayed:
                self.simulator.call_loop()

            # 绘制矩阵
            self.draw_matrix()

            # 绘制信息
            self.draw_info()

            # 更新显示
            pygame.display.flip()

            # 控制帧率
            self.clock.tick(FPS)

            if self.simulator.time_api.delay_until > time.time():
                # 如果有延迟，暂停loop调用
                self.delayed = True
            else:
                self.delayed = False

        # 退出
        self.simulator.call_unload()
        pygame.quit()

def main():
    """主函数"""
    import argparse

    parser = argparse.ArgumentParser(description='ESP32 Lua LED Matrix Simulator')
    parser.add_argument('lua_file', help='Lua脚本文件路径')
    parser.add_argument('--config', help='配置文件路径(.cfg)')
    parser.add_argument('--width', type=int, default=32, help='矩阵宽度')
    parser.add_argument('--height', type=int, default=8, help='矩阵高度')

    args = parser.parse_args()

    # 创建模拟器
    simulator = LuaSimulator(args.width, args.height)

    # 加载配置
    if args.config:
        simulator.load_config(args.config)

    # 加载Lua文件
    if not simulator.load_lua_file(args.lua_file):
        return

    # 创建GUI并运行
    gui = SimulatorGUI(simulator)
    gui.run()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("ESP32 Lua LED Matrix Simulator")
        print("用法: python simulator.py <lua文件> [--config <配置文件>] [--width 32] [--height 8]")
        print()
        print("示例:")
        print("  python simulator.py ../../data/rhythm-long.lua --config ../../data/rhythm-long.cfg")
        print("  python simulator.py ../../data/colorwave-long.lua --width 32 --height 8")
        sys.exit(1)

    main()
