# Bottle Lua 脚本开发指南

本指南介绍如何为 Bottle 设备开发 Lua 脚本模块。

## 目录

- [快速开始](#快速开始)
- [脚本结构](#脚本结构)
- [资源声明](#资源声明)
- [API 参考](#api-参考)
- [示例模块](#示例模块)
- [最佳实践](#最佳实践)

---

## 快速开始

### 基本模板

```lua
-- @name: My Module
-- @version: 1.0.0
-- @author: Your Name
-- @description: Module description
-- @id: my_module

function setup()
  -- 初始化代码
  print("Module loaded")
end

function loop()
  -- 主循环代码
  led.clear()
  led.set(0, 0, 255, 0, 0)  -- 设置红色像素
  led.show()
  time.delay(100)
end
```

### 元数据注释

脚本开头的注释用于描述模块信息：

- `@name`: 模块名称
- `@version`: 版本号
- `@author`: 作者
- `@description`: 描述
- `@id`: 唯一标识符

---

## 脚本结构

### 必需函数

#### `setup()`
模块加载时调用一次，用于初始化。

```lua
function setup()
  print("Initializing...")
  -- 声明需要的硬件资源
  use("gravity")
  use("audio")
end
```

#### `loop()`
主循环函数，持续调用。

```lua
function loop()
  -- 更新逻辑
  led.clear()
  -- 绘制内容
  led.show()
  time.delay(50)  -- 控制帧率
end
```

---

## 资源声明

使用 `use()` 函数声明模块需要的硬件资源。引擎会自动管理资源的启动和停止。

### 语法

```lua
use(resource_name)
```

### 支持的资源

#### 重力传感器

```lua
function setup()
  use("gravity")  -- 启动重力传感器
end

function loop()
  local gx, gy, gz, valid = gravity.get()
  if valid then
    print("Gravity:", gx, gy, gz)
  end
end
```

#### 音频 FFT

```lua
function setup()
  use("audio")  -- 启动音频采集和 FFT
end

function loop()
  local bass = audio.get_band(0)
  local mid = audio.get_band(1)
  local treble = audio.get_band(2)
  print("Audio:", bass, mid, treble)
end
```

### 资源管理

- 在 `setup()` 中调用 `use()` 声明资源
- 引擎自动在模块加载时启动资源
- 引擎自动在模块卸载时停止资源
- 不需要手动管理资源生命周期

---

## API 参考

### LED 控制

#### `led.clear()`
清空所有 LED。

```lua
led.clear()
```

#### `led.set(x, y, r, g, b)`
设置指定位置的 LED 颜色。

参数：
- `x`: X 坐标 (0-15)
- `y`: Y 坐标 (0-7)
- `r`: 红色分量 (0-255)
- `g`: 绿色分量 (0-255)
- `b`: 蓝色分量 (0-255)

```lua
led.set(0, 0, 255, 0, 0)  -- 红色
led.set(1, 0, 0, 255, 0)  -- 绿色
led.set(2, 0, 0, 0, 255)  -- 蓝色
```

#### `led.show()`
刷新 LED 显示。

```lua
led.show()
```

#### `led.set_brightness(brightness)`
设置全局亮度。

参数：
- `brightness`: 亮度值 (0-255)

```lua
led.set_brightness(128)  -- 50% 亮度
```

---

### 时间控制

#### `time.delay(ms)`
延迟指定毫秒数。

参数：
- `ms`: 延迟时间（毫秒）

```lua
time.delay(100)  -- 延迟 100ms
```

**注意**：`time.delay()` 会阻塞执行。对于动画效果，建议使用较小的延迟值（如 30-50ms）以保持流畅。

#### `time.millis()`
获取系统运行时间（毫秒）。

```lua
local now = time.millis()
print("Uptime:", now, "ms")
```

---

### 重力传感器

**需要声明**：在 `setup()` 中调用 `use("gravity")`

#### `gravity.get()`
获取重力传感器数据。

返回值：
- `gx`: X 轴重力加速度
- `gy`: Y 轴重力加速度
- `gz`: Z 轴重力加速度
- `valid`: 数据是否有效（布尔值）

```lua
function setup()
  use("gravity")
end

function loop()
  local gx, gy, gz, valid = gravity.get()
  if valid then
    print(string.format("G: %.2f, %.2f, %.2f", gx, gy, gz))
  end
end
```

---

### 音频 FFT

**需要声明**：在 `setup()` 中调用 `use("audio")`

#### `audio.get_band(band_index)`
获取指定频段的能量值。

参数：
- `band_index`: 频段索引 (0-2)
  - 0: 低音 (bass)
  - 1: 中音 (mid)
  - 2: 高音 (treble)

返回值：
- 能量值 (0-255)

```lua
function setup()
  use("audio")
end

function loop()
  local bass = audio.get_band(0)
  local mid = audio.get_band(1)
  local treble = audio.get_band(2)
  
  -- 根据音频绘制可视化效果
  led.clear()
  for x = 0, 15 do
    local height = math.floor(bass / 32)
    for y = 0, height do
      led.set(x, y, 255, 0, 0)
    end
  end
  led.show()
end
```

---

### 数学函数

#### `math.random([m [, n]])`
生成随机数。

```lua
local r1 = math.random()        -- 0-1 之间的浮点数
local r2 = math.random(10)      -- 1-10 之间的整数
local r3 = math.random(5, 15)   -- 5-15 之间的整数
```

#### `math.clamp(value, min, max)`
将值限制在指定范围内。

```lua
local v = math.clamp(150, 0, 100)  -- 返回 100
```

#### 其他数学函数

```lua
math.floor(x)   -- 向下取整
math.ceil(x)    -- 向上取整
math.abs(x)     -- 绝对值
math.sin(x)     -- 正弦
math.cos(x)     -- 余弦
math.sqrt(x)    -- 平方根
math.pi         -- 圆周率
```

---

### 字符串函数

#### `string.format(format, ...)`
格式化字符串。

```lua
local s = string.format("Value: %.2f", 3.14159)
print(s)  -- "Value: 3.14"
```

#### 其他字符串函数

```lua
string.len(s)       -- 字符串长度
string.sub(s, i, j) -- 子字符串
string.upper(s)     -- 转大写
string.lower(s)     -- 转小写
```

---

### 表操作

#### `table.insert(table, [pos,] value)`
插入元素。

```lua
local t = {1, 2, 3}
table.insert(t, 4)      -- {1, 2, 3, 4}
table.insert(t, 2, 99)  -- {1, 99, 2, 3, 4}
```

#### `table.remove(table [, pos])`
移除元素。

```lua
local t = {1, 2, 3, 4}
table.remove(t)     -- 移除最后一个，返回 4
table.remove(t, 2)  -- 移除索引 2，返回 2
```

---

## 示例模块

### 示例 1：呼吸灯

```lua
-- @name: Breathing Light
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: Smooth breathing effect
-- @id: breathing

local brightness = 0
local direction = 1

function setup()
  print("Breathing light loaded")
end

function loop()
  led.clear()
  
  -- 更新亮度
  brightness = brightness + direction * 5
  if brightness >= 255 then
    brightness = 255
    direction = -1
  elseif brightness <= 0 then
    brightness = 0
    direction = 1
  end
  
  -- 绘制
  for x = 0, 15 do
    for y = 0, 7 do
      led.set(x, y, brightness, brightness / 2, 0)
    end
  end
  
  led.show()
  time.delay(20)
end

function unload()
  print("Breathing light unloaded")
end
```

### 示例 2：音乐可视化

```lua
-- @name: Music Visualizer
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: Audio spectrum visualizer
-- @id: music_viz

function setup()
  use("audio")  -- 声明需要音频资源
  print("Music visualizer loaded")
end

function loop()
  led.clear()
  
  -- 获取三个频段
  local bass = audio.get_band(0)
  local mid = audio.get_band(1)
  local treble = audio.get_band(2)
  
  -- 绘制频谱
  local bass_height = math.floor(bass / 32)
  local mid_height = math.floor(mid / 32)
  local treble_height = math.floor(treble / 32)
  
  -- 低音（红色）
  for y = 0, bass_height do
    for x = 0, 4 do
      led.set(x, y, 255, 0, 0)
    end
  end
  
  -- 中音（绿色）
  for y = 0, mid_height do
    for x = 5, 9 do
      led.set(x, y, 0, 255, 0)
    end
  end
  
  -- 高音（蓝色）
  for y = 0, treble_height do
    for x = 10, 15 do
      led.set(x, y, 0, 0, 255)
    end
  end
  
  led.show()
  time.delay(30)
end
```

### 示例 3：摇晃检测

```lua
-- @name: Shake Detector
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: Detects shake gestures
-- @id: shake

local last_gx, last_gy, last_gz = 0, 0, 0
local shake_threshold = 0.1
local shake_count = 0

function setup()
  use("gravity")  -- 声明需要重力传感器
  print("Shake detector loaded")
  
  local gx, gy, gz, valid = gravity.get()
  if valid then
    last_gx, last_gy, last_gz = gx, gy, gz
  end
end

function loop()
  local gx, gy, gz, valid = gravity.get()
  
  if valid then
    -- 计算变化量
    local delta = math.abs(gx - last_gx) + 
                  math.abs(gy - last_gy) + 
                  math.abs(gz - last_gz)
    
    -- 检测摇晃
    if delta > shake_threshold then
      shake_count = shake_count + 1
      print("Shake detected! Count:", shake_count)
      
      -- 闪烁效果
      led.clear()
      for x = 0, 15 do
        for y = 0, 7 do
          led.set(x, y, 255, 255, 255)
        end
      end
      led.show()
      time.delay(100)
    end
    
    last_gx, last_gy, last_gz = gx, gy, gz
  end
  
  led.clear()
  led.show()
  time.delay(50)
end
```

---

## 最佳实践

### 1. 性能优化

- 使用合理的 `time.delay()` 值（30-50ms）平衡流畅度和性能
- 避免在 `loop()` 中进行复杂计算
- 重用变量，减少内存分配

```lua
-- 好的做法
local temp_r, temp_g, temp_b = 0, 0, 0

function loop()
  for x = 0, 15 do
    temp_r = math.random(0, 255)
    led.set(x, 0, temp_r, 0, 0)
  end
end

-- 避免
function loop()
  for x = 0, 15 do
    local r = math.random(0, 255)  -- 每次循环都创建新变量
    led.set(x, 0, r, 0, 0)
  end
end
```

### 2. 资源管理

- 在 `setup()` 中声明所有需要的资源
- 不要忘记调用 `use()` 声明硬件资源
- 引擎会自动管理资源生命周期

```lua
function setup()
  -- 声明所有需要的资源
  use("gravity")
  use("audio")
end
```

### 3. 错误处理

- 检查传感器数据的有效性
- 使用 `math.clamp()` 限制数值范围
- 添加边界检查

```lua
function loop()
  local gx, gy, gz, valid = gravity.get()
  
  if valid then
    -- 使用数据前检查有效性
    local x = math.clamp(math.floor(gx * 8 + 8), 0, 15)
    led.set(x, 0, 255, 0, 0)
  end
end
```

### 4. 代码组织

- 使用有意义的变量名
- 添加必要的注释
- 将复杂逻辑封装成函数

```lua
-- 辅助函数
local function draw_bar(x, height, r, g, b)
  for y = 0, height do
    led.set(x, y, r, g, b)
  end
end

function loop()
  local bass = audio.get_band(0)
  local height = math.floor(bass / 32)
  draw_bar(0, height, 255, 0, 0)
  led.show()
end
```

### 5. 调试技巧

- 使用 `print()` 输出调试信息
- 检查串口输出查看日志
- 逐步测试功能

```lua
function setup()
  print("=== Module Starting ===")
  use("gravity")
  print("Gravity sensor initialized")
end

function loop()
  local gx, gy, gz, valid = gravity.get()
  if valid then
    print(string.format("G: %.2f, %.2f, %.2f", gx, gy, gz))
  else
    print("Invalid gravity data")
  end
  time.delay(1000)
end
```

---

## 常见问题

### Q: 为什么重力传感器返回的数据全是 0？
A: 确保在 `setup()` 中调用了 `use("gravity")` 声明资源。

### Q: 音频可视化没有反应？
A: 确保在 `setup()` 中调用了 `use("audio")` 声明资源。

### Q: LED 显示不正常？
A: 检查是否调用了 `led.show()` 刷新显示。

### Q: 如何控制动画速度？
A: 调整 `time.delay()` 的参数值，值越小动画越快。

### Q: 如何节省电量？
A: 只声明需要的资源，引擎会在模块切换时自动停止未使用的资源。

---

## 更多资源

- 查看设备内置的示例模块
- 参考应用市场中的开源模块
- 加入开发者社区交流经验

---

**Happy Coding! 🎨**
