#!/usr/bin/env python3
"""
完整版Lua模拟器 - 使用lupa支持真实Lua代码执行
需要安装: pip install lupa
"""

import math
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

try:
    from lupa import LuaRuntime
    HAS_LUPA = True
except ImportError:
    HAS_LUPA = False
    print("警告: 未安装lupa，无法执行真实Lua代码")
    print("安装方法: pip install lupa")

from simulator import LuaSimulator, SimulatorGUI
import argparse

class LupaLuaSimulator(LuaSimulator):
    """使用Lupa的完整Lua模拟器"""

    def __init__(self, width, height):
        super().__init__(width, height)

        if not HAS_LUPA:
            raise ImportError("需要安装lupa: pip install lupa")

        # 创建Lua运行时
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua_env = self.lua.globals()

        # 注入全局变量和API
        self._inject_lua_api()

    def _inject_lua_api(self):
        """注入API到Lua环境"""
        # 全局常量
        self.lua_env.WIDTH = self.width
        self.lua_env.HEIGHT = self.height

        # CONFIG表
        config_table = self.lua.table()
        self.lua_env.CONFIG = config_table

        # LED API
        led_table = self.lua.table()
        led_table.set = self.led_api.set
        led_table.clear = self.led_api.clear
        led_table.show = self.led_api.show
        led_table.hsv = self.led_api.hsv
        led_table.text = self.led_api.text
        led_table.palette = self.led_api.palette
        self.lua_env.led = led_table

        # FFT API
        fft_table = self.lua.table()
        fft_table.get = self.fft_api.get
        fft_table.count = self.fft_api.count
        self.lua_env.fft = fft_table

        # Gravity API
        gravity_table = self.lua.table()
        gravity_table.get = self.gravity_api.get
        self.lua_env.gravity = gravity_table

        # Button API
        button_table = self.lua.table()
        button_table.poll = self.button_api.poll
        button_table.is_holding = self.button_api.is_holding
        self.lua_env.button = button_table

        # Time API
        time_table = self.lua.table()
        time_table.millis = self.time_api.millis
        time_table.delay = self.time_api.delay
        time_table.now = lambda: self._convert_time_dict(self.time_api.now())
        self.lua_env.time = time_table

        # Math库
        self.lua_env.math = self.lua.eval('math')
        self.lua_env.math.clamp = lambda x, a, b: max(a, min(x, b))
        self.lua_env.math.pow = lambda x, y: x ** y
        self.lua_env.math.atan2 = lambda y, x: math.atan2(y, x)

        # Print函数
        self.lua_env.print = print

        # Use函数（资源声明）
        self.lua_env.use = lambda x: None

    def _convert_time_dict(self, time_dict):
        """将Python字典转换为Lua表"""
        lua_table = self.lua.table()
        for key, value in time_dict.items():
            lua_table[key] = value
        return lua_table

    def load_lua_file(self, filepath):
        """加载并执行Lua文件"""
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                lua_code = f.read()

            # 执行Lua代码
            self.lua.execute(lua_code)

            # 获取函数引用
            if 'setup' in self.lua_env:
                self.setup_func = self.lua_env.setup
            if 'loop' in self.lua_env:
                self.loop_func = self.lua_env.loop
            if 'unload' in self.lua_env:
                self.unload_func = self.lua_env.unload

            print(f"✓ 已加载Lua文件: {filepath}")
            if self.setup_func:
                print("  - 找到 setup() 函数")
            if self.loop_func:
                print("  - 找到 loop() 函数")
            if self.unload_func:
                print("  - 找到 unload() 函数")

            return True

        except Exception as e:
            print(f"✗ 加载失败: {e}")
            import traceback
            traceback.print_exc()
            return False

    def load_config(self, config_file):
        """加载配置文件到Lua CONFIG表"""
        import json

        try:
            with open(config_file, 'r', encoding='utf-8') as f:
                config_data = json.load(f)

            # 转换配置为Lua表
            for item in config_data:
                key = item.get('key')
                default = item.get('default')
                if key and default is not None:
                    self.lua_env.CONFIG[key] = default

            print(f"✓ 已加载配置: {len(config_data)} 项")
            return True

        except Exception as e:
            print(f"✗ 配置加载失败: {e}")
            return False

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='ESP32 Lua模拟器 (Lupa版本)')
    parser.add_argument('lua_file', help='Lua脚本文件路径')
    parser.add_argument('--config', help='配置文件路径(.cfg)')
    parser.add_argument('--width', type=int, default=32, help='矩阵宽度')
    parser.add_argument('--height', type=int, default=8, help='矩阵高度')

    args = parser.parse_args()

    if not HAS_LUPA:
        print("\n错误: 需要安装lupa库才能运行此版本")
        print("安装命令: pip install lupa")
        print("\n或者使用基础版模拟器: python simulator.py")
        sys.exit(1)

    print("=" * 60)
    print("ESP32 Lua模拟器 (完整版 - 支持真实Lua代码)")
    print("=" * 60)
    print()

    # 创建模拟器
    simulator = LupaLuaSimulator(args.width, args.height)

    # 加载配置
    if args.config and os.path.exists(args.config):
        simulator.load_config(args.config)

    # 加载Lua文件
    if not simulator.load_lua_file(args.lua_file):
        print("\nLua文件加载失败，退出")
        sys.exit(1)

    print()
    print("控制说明:")
    print("  空格键 - 暂停/恢复")
    print("  R键 - 重新加载脚本")
    print("  方向键 - 模拟重力倾斜")
    print("  鼠标左键 - 模拟按键")
    print("  ESC - 退出")
    print()
    print("=" * 60)
    print()

    # 创建GUI并运行
    gui = SimulatorGUI(simulator)
    gui.run()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("ESP32 Lua模拟器 (完整版)")
        print()
        print("用法: python simulator_full.py <lua文件> [选项]")
        print()
        print("示例:")
        print("  python simulator_full.py ../../data/rhythm-long.lua --config ../../data/rhythm-long.cfg")
        print("  python simulator_full.py ../../data/colorwave.lua --width 32 --height 8")
        print()
        print("注意: 需要安装lupa库")
        print("      pip install lupa")
        sys.exit(1)

    main()
