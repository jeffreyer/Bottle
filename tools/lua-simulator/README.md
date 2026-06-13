# ESP32 Lua LED Matrix Simulator

PC端ESP32 Lua动画模拟器，用于提高Lua脚本开发效率。

## 功能特性

✅ **可视化LED矩阵** - 实时显示32x8或17x8 LED动画效果  
✅ **完整API模拟** - 支持led、fft、gravity、button、time等所有API  
✅ **交互控制** - 键盘鼠标模拟重力感应和按键  
✅ **热重载** - 按R键快速重新加载脚本  
✅ **配置支持** - 自动加载.cfg配置文件  
✅ **暂停/恢复** - 空格键暂停动画观察细节  

## 安装依赖

```bash
pip install pygame
```

**可选：完整Lua支持**
```bash
pip install lupa
```

## 使用方法

### 基本用法

```bash
# 运行长版音乐律动
python simulator.py ../../data/rhythm-long.lua --config ../../data/rhythm-long.cfg

# 运行彩色波浪（32x8）
python simulator.py ../../data/colorwave.lua --width 32 --height 8

# 运行短版时钟（17x8）
python simulator.py ../../data/clock.lua --width 17 --height 8
```

### 命令行参数

```
python simulator.py <lua文件> [选项]

必需参数:
  lua文件          要运行的Lua脚本文件路径

可选参数:
  --config FILE    配置文件路径(.cfg格式)
  --width N        LED矩阵宽度（默认32）
  --height N       LED矩阵高度（默认8）
```

## 键盘控制

| 按键 | 功能 |
|------|------|
| **空格** | 暂停/恢复动画 |
| **R** | 重新加载Lua脚本（重新调用setup） |
| **↑** | 模拟设备向前倾斜（gx=0.5） |
| **↓** | 模拟设备向后倾斜（gx=-0.5） |
| **←** | 模拟设备向左倾斜（gy=-0.5） |
| **→** | 模拟设备向右倾斜（gy=0.5） |
| **鼠标左键** | 模拟按键单击/长按 |
| **ESC** | 退出模拟器 |

## 支持的API

### LED控制
- ✅ `led.set(x, y, r, g, b)` - 设置单个像素
- ✅ `led.clear()` - 清空显示
- ✅ `led.show()` - 刷新显示
- ✅ `led.hsv(h, s, v)` - HSV转RGB
- ✅ `led.text(x, y, text, r, g, b)` - 绘制文本
- ✅ `led.palette(palette, index, brightness)` - 调色板

### 音频FFT
- ✅ `fft.get(index)` - 获取频段值（模拟数据）
- ✅ `fft.count()` - 返回512

### 重力感应
- ✅ `gravity.get()` - 返回gx, gy, gz, valid
- 🎮 方向键控制倾斜

### 按键
- ✅ `button.poll()` - 获取按键事件
- ✅ `button.is_holding()` - 是否按住
- 🎮 鼠标左键模拟

### 时间
- ✅ `time.millis()` - 毫秒时间戳
- ✅ `time.delay(ms)` - 延迟（在模拟器中忽略）
- ✅ `time.now()` - 当前时间

### 全局变量
- ✅ `WIDTH` - 矩阵宽度
- ✅ `HEIGHT` - 矩阵高度
- ✅ `CONFIG` - 配置表

## 示例

### 示例1：测试彩色波浪

```bash
cd tools/lua-simulator
python simulator.py test_colorwave.py
```

### 示例2：测试自己的脚本

创建 `my_animation.lua`:
```lua
-- @name: 我的动画
-- @id: my-animation

function setup()
  print("动画初始化")
end

function loop()
  led.clear()
  
  -- 绘制一个移动的点
  local x = math.floor((time.millis() / 50) % WIDTH)
  led.set(x, HEIGHT / 2, 255, 0, 0)
  
  led.show()
  time.delay(30)
end

function unload()
  print("动画卸载")
end
```

运行：
```bash
python simulator.py my_animation.lua
```

## 项目结构

```
tools/lua-simulator/
├── simulator.py          # 主模拟器程序
├── README.md            # 本文件
├── test_colorwave.py    # 测试脚本：彩色波浪
└── requirements.txt     # Python依赖
```

## 配置文件格式

配置文件采用JSON格式（.cfg扩展名）：

```json
[
  {
    "key": "move_interval",
    "type": "slider",
    "label": "滚动速度",
    "default": 30,
    "min": 1,
    "max": 60
  },
  {
    "key": "main_color",
    "type": "color",
    "label": "主要颜色",
    "default": "#FF8800"
  }
]
```

模拟器会自动提取 `key` 和 `default` 字段到 `CONFIG` 表。

## 限制和注意事项

### 当前限制

1. **不是真正的Lua解释器** - 当前版本使用Python模拟，不能运行实际的Lua代码
2. **FFT数据为模拟** - 音频数据使用数学函数模拟，不是真实麦克风输入
3. **文字渲染简化** - 仅支持数字和部分大写字母
4. **性能差异** - PC上的表现可能与实际ESP32设备不同

### 升级到完整Lua支持

要支持真正的Lua代码执行，需要：

1. 安装lupa: `pip install lupa`
2. 修改 `simulator.py` 中的Lua执行部分
3. 使用lupa的LuaRuntime替代Python模拟

示例代码片段（需要集成到simulator.py）：
```python
from lupa import LuaRuntime

lua = LuaRuntime(unpack_returned_tuples=True)

# 注入API到Lua环境
lua_globals = lua.globals()
lua_globals.WIDTH = width
lua_globals.HEIGHT = height
lua_globals.led = led_api
lua_globals.fft = fft_api
# ... 其他API

# 执行Lua代码
lua.execute(lua_code)

# 调用Lua函数
if 'setup' in lua_globals:
    lua_globals.setup()
```

## 开发工作流

### 推荐工作流程

1. **在模拟器中快速迭代** - 修改Lua代码，按R键热重载
2. **验证动画效果** - 使用键盘模拟重力，鼠标模拟按键
3. **调整配置参数** - 修改.cfg文件，重启模拟器查看效果
4. **上传到设备测试** - 确认无误后上传到ESP32实际测试

### 调试技巧

1. **使用print调试** - print输出会显示在终端
2. **暂停观察** - 空格键暂停，仔细观察每一帧
3. **修改像素大小** - 编辑simulator.py中的PIXEL_SIZE参数
4. **降低帧率** - 编辑FPS参数，方便观察慢动画

## 扩展功能

### 添加真实音频输入

可以集成 `pyaudio` 实现真实FFT：

```python
import pyaudio
import numpy as np

# 音频回调
def audio_callback(in_data, frame_count, time_info, status):
    audio_data = np.frombuffer(in_data, dtype=np.int16)
    fft_data = np.fft.fft(audio_data)
    fft_api.data = np.abs(fft_data[:512])
    return (in_data, pyaudio.paContinue)
```

### 添加视频录制

使用 `pygame-recorder` 或 `opencv` 录制动画为视频文件。

### 添加性能分析

统计每帧执行时间，找出性能瓶颈：

```python
import cProfile
cProfile.run('simulator.call_loop()')
```

## 故障排除

### Q: 窗口显示不正常
A: 检查分辨率设置，尝试调整PIXEL_SIZE和PIXEL_GAP参数

### Q: 帧率很低
A: 降低FPS参数，或优化Lua代码中的循环

### Q: Lua脚本报错
A: 当前版本不支持真实Lua执行，需要安装lupa库

### Q: FFT数据不变化
A: 当前为模拟数据，可以集成pyaudio获取真实音频

## 贡献

欢迎提交Issue和Pull Request改进模拟器！
