# Lua 脚本开发指南

本指南介绍如何为 Bottle 设备开发 Lua 模块，包括可用的 API、配置系统、最佳实践等。

## 目录

- [快速开始](#快速开始)
- [模块结构](#模块结构)
- [配置系统](#配置系统)
- [硬件 API](#硬件-api)
- [最佳实践](#最佳实践)
- [示例模块](#示例模块)

---

## 快速开始

### 最小模块示例

```lua
-- @name: 我的模块
-- @version: 1.0.0
-- @author: Your Name
-- @description: 模块描述
-- @id: mymodule

function setup()
  print("Module loaded")
  led.clear()
  led.show()
end

function loop()
  -- 主循环逻辑
  led.set(0, 0, 255, 0, 0)  -- 设置第一个 LED 为红色
  led.show()
  time.delay(100)
end

function unload()
  print("Module unloaded")
  led.clear()
  led.show()
end
```

### 模块元数据

每个 Lua 模块必须在文件开头包含元数据注释：

```lua
-- @name: 显示名称（必需）
-- @version: 版本号（必需）
-- @author: 作者名称（必需）
-- @description: 英文描述（必需）
-- @id: 模块唯一标识符（必需，只能包含字母、数字、下划线，最长8个字符）
```

**重要**：模块 ID 必须简短（最长8个字符），因为配置键名 `cfg_<module_id>` 受 NVS 15字符限制。

---

## 模块结构

### 生命周期函数

#### `setup()`
- 模块加载时调用一次
- 用于初始化状态、加载配置、设置硬件
- 可选函数

```lua
function setup()
  -- 初始化变量
  local config_value = (CONFIG and CONFIG.my_setting) or 100
  
  -- 初始化硬件
  led.clear()
  led.show()
end
```

#### `loop()`
- 主循环，持续调用
- 实现模块的主要逻辑
- 必需函数

```lua
function loop()
  -- 更新显示
  led.clear()
  -- ... 渲染逻辑
  led.show()
  
  -- 控制帧率
  time.delay(30)
end
```

#### `unload()`
- 模块卸载时调用
- 用于清理资源、保存状态
- 可选函数

```lua
function unload()
  print("Cleaning up...")
  led.clear()
  led.show()
end
```

---

## 配置系统

### 读取配置

配置通过全局 `CONFIG` 表访问：

```lua
-- 读取配置值，提供默认值
local text = (CONFIG and CONFIG.text) or "Hello"
local speed = (CONFIG and CONFIG.speed) or 100
local color = (CONFIG and CONFIG.color) or "#00FF00"
local mode = (CONFIG and CONFIG.mode) or "auto"
```

**注意**：始终检查 `CONFIG` 是否存在，并提供默认值。

### 配置定义

配置在云数据库的模块定义中声明：

```javascript
config: [
  {
    key: "text",           // 配置键名
    label: "显示文本",      // 显示标签
    type: "text",          // 配置类型
    default: "Hello",      // 默认值
    desc: "要显示的文本内容"  // 描述
  },
  {
    key: "speed",
    label: "速度",
    type: "slider",
    default: 100,
    min: 50,
    max: 500,
    step: 25,
    unit: "ms",
    desc: "动画速度"
  },
  {
    key: "color",
    label: "颜色",
    type: "color",
    default: "#00FF00",
    desc: "LED 颜色"
  },
  {
    key: "mode",
    label: "模式",
    type: "select",
    default: "auto",
    options: [
      { value: "static", label: "静态" },
      { value: "auto", label: "自动" }
    ],
    desc: "显示模式"
  },
  {
    key: "enabled",
    label: "启用",
    type: "switch",
    default: 1,
    desc: "是否启用功能"
  }
]
```

### 配置类型

| 类型 | 值类型 | 说明 | 示例 |
|------|--------|------|------|
| `text` | string | 文本输入 | `"Hello World"` |
| `number` | number | 数字输入 | `42`, `3.14` |
| `slider` | number | 滑块选择 | `100` (需要 min/max/step) |
| `color` | string | 颜色选择器 | `"#FF0000"` |
| `select` | string/number | 下拉选择 | `"option1"` 或 `0` |
| `switch` | number | 开关 | `0` (关) 或 `1` (开) |

### 解析颜色值

```lua
-- 解析十六进制颜色字符串
local function parse_color(hex)
  if type(hex) ~= "string" then
    return 0, 255, 0  -- 默认绿色
  end

  hex = hex:gsub("#", "")
  if #hex ~= 6 then
    return 0, 255, 0
  end

  local r = tonumber(hex:sub(1, 2), 16) or 0
  local g = tonumber(hex:sub(3, 4), 16) or 255
  local b = tonumber(hex:sub(5, 6), 16) or 0

  return r, g, b
end

-- 使用
local color = (CONFIG and CONFIG.color) or "#00FF00"
local r, g, b = parse_color(color)
```

---

## 硬件 API

### LED 控制 (`led`)

#### `led.clear()`
清除所有 LED（设置为黑色）

```lua
led.clear()
```

#### `led.show()`
更新 LED 显示（将缓冲区内容输出到硬件）

```lua
led.show()
```

**重要**：修改 LED 后必须调用 `led.show()` 才能看到效果。

#### `led.set(x, y, r, g, b)`
设置单个 LED 的颜色

- `x`: X 坐标 (0-16)
- `y`: Y 坐标 (0-7)
- `r`: 红色分量 (0-255)
- `g`: 绿色分量 (0-255)
- `b`: 蓝色分量 (0-255)

```lua
led.set(0, 0, 255, 0, 0)  -- 左上角设为红色
led.set(8, 4, 0, 255, 0)  -- 中心设为绿色
```

#### `led.text(x, y, text, r, g, b)`
在指定位置绘制文本

- `x`: 起始 X 坐标
- `y`: 起始 Y 坐标
- `text`: 要显示的文本（支持 ASCII 0x20-0x7A）
- `r, g, b`: 文字颜色

```lua
led.text(0, 1, "Hello", 0, 255, 0)  -- 绿色文字
led.text(0, 1, "Score: 10", 255, 255, 0)  -- 黄色文字
```

**字体特性**：
- 3x5 像素字体
- 支持大写字母 A-Z
- 支持小写字母 a-z（较小）
- 支持数字 0-9
- 支持常用符号（空格、标点、运算符等）
- 字符间距：4 像素（3 像素字符 + 1 像素间距）

#### `led.hsv(h, s, v)`
将 HSV 颜色转换为 RGB

- `h`: 色相 (0-255)
- `s`: 饱和度 (0-255)
- `v`: 明度 (0-255)
- 返回：`r, g, b` (0-255)

```lua
local r, g, b = led.hsv(128, 255, 255)  -- 青色
led.set(0, 0, r, g, b)
```

### 时间控制 (`time`)

#### `time.millis()`
获取系统运行时间（毫秒）

```lua
local now = time.millis()
if now - last_update > 1000 then
  -- 每秒执行一次
  last_update = now
end
```

#### `time.delay(ms)`
延迟指定毫秒数

```lua
time.delay(100)  -- 延迟 100ms
```

**注意**：`time.delay()` 会阻塞执行，建议使用 `time.millis()` 实现非阻塞延迟。

### 重力感应 (`gravity`)

#### 声明使用

```lua
use("gravity")  -- 在文件开头声明
```

#### `gravity.get()`
获取重力感应数据

返回：`gx, gy, gz, valid`
- `gx`: X 轴加速度 (-1.0 到 1.0)
- `gy`: Y 轴加速度 (-1.0 到 1.0)
- `gz`: Z 轴加速度 (-1.0 到 1.0)
- `valid`: 数据是否有效 (boolean)

```lua
local gx, gy, gz, valid = gravity.get()
if valid then
  if math.abs(gx) > 0.3 then
    -- 设备向左或向右倾斜
  end
end
```

### 音频频谱 (`audio`)

#### 声明使用

```lua
use("audio")  -- 在文件开头声明
```

#### `audio.init()`
初始化音频采集

```lua
function setup()
  audio.init()
end
```

#### `audio.getSpectrum()`
获取音频频谱数据

返回：包含 17 个频段的表，每个值范围 0.0-1.0

```lua
local spectrum = audio.getSpectrum()
if spectrum then
  for i = 0, 16 do
    local amplitude = spectrum[i] or 0
    -- 使用频谱数据
  end
end
```

#### `audio.close()`
关闭音频采集

```lua
function unload()
  audio.close()
end
```

---

## 最佳实践

### 1. 性能优化

#### 使用局部变量
```lua
-- 好
local function update()
  local x = 0
  for i = 1, 100 do
    x = x + i
  end
end

-- 避免
function update()
  x = 0  -- 全局变量，较慢
  for i = 1, 100 do
    x = x + i
  end
end
```

#### 缓存函数引用
```lua
-- 好
local led_set = led.set
for y = 0, 7 do
  for x = 0, 16 do
    led_set(x, y, 255, 0, 0)
  end
end

-- 避免
for y = 0, 7 do
  for x = 0, 16 do
    led.set(x, y, 255, 0, 0)  -- 每次查找 led.set
  end
end
```

#### 控制帧率
```lua
function loop()
  -- 渲染逻辑
  led.show()
  time.delay(30)  -- ~33 FPS
end
```

### 2. 内存管理

#### 避免频繁创建表
```lua
-- 好：复用表
local temp_table = {}
function loop()
  -- 清空并复用
  for k in pairs(temp_table) do
    temp_table[k] = nil
  end
  -- 使用 temp_table
end

-- 避免：每次创建新表
function loop()
  local temp_table = {}  -- 每帧创建，增加 GC 压力
end
```

#### 预分配数组
```lua
-- 好
local particles = {}
for i = 1, 100 do
  particles[i] = {x = 0, y = 0, vx = 0, vy = 0}
end

-- 避免在循环中动态增长
```

### 3. 代码组织

#### 使用模块化函数
```lua
local function init_particles()
  -- 初始化逻辑
end

local function update_particles()
  -- 更新逻辑
end

local function render_particles()
  -- 渲染逻辑
end

function setup()
  init_particles()
end

function loop()
  update_particles()
  render_particles()
  led.show()
  time.delay(30)
end
```

#### 使用常量
```lua
-- 在文件顶部定义常量
local WIDTH = 17
local HEIGHT = 8
local MAX_PARTICLES = 50
local UPDATE_INTERVAL = 100

-- 使用常量
for x = 0, WIDTH - 1 do
  for y = 0, HEIGHT - 1 do
    -- ...
  end
end
```

### 4. 错误处理

#### 检查配置值
```lua
local speed = (CONFIG and CONFIG.speed) or 100
if speed < 10 then speed = 10 end
if speed > 1000 then speed = 1000 end
```

#### 验证硬件数据
```lua
local gx, gy, gz, valid = gravity.get()
if not valid then
  -- 使用默认值或跳过
  return
end
```

### 5. 调试技巧

#### 使用 print 输出
```lua
function setup()
  print("Module loaded")
  print("Config value:", CONFIG and CONFIG.speed or "not set")
end

function loop()
  local gx, gy = gravity.get()
  if millis() % 1000 == 0 then
    print("Gravity:", gx, gy)
  end
end
```

#### 输出到串口监视器
设备通过串口输出调试信息，可以在开发工具中查看。

---

## 示例模块

### 示例 1：文字滚动显示

```lua
-- @name: 文字显示
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: Display custom text with scrolling support
-- @id: textshow

-- 加载配置
local text = (CONFIG and CONFIG.text) or "Hello"
local text_color = (CONFIG and CONFIG.text_color) or "#00FF00"
local scroll_speed = (CONFIG and CONFIG.scroll_speed) or 100
local scroll_mode = (CONFIG and CONFIG.scroll_mode) or "auto"
local y_position = (CONFIG and CONFIG.y_position) or 1

-- 解析颜色
local function parse_color(hex)
  if type(hex) ~= "string" then
    return 0, 255, 0
  end
  hex = hex:gsub("#", "")
  if #hex ~= 6 then
    return 0, 255, 0
  end
  local r = tonumber(hex:sub(1, 2), 16) or 0
  local g = tonumber(hex:sub(3, 4), 16) or 255
  local b = tonumber(hex:sub(5, 6), 16) or 0
  return r, g, b
end

-- 状态变量
local scroll_offset = 0
local last_scroll_time = 0
local text_width = #text * 4
local r, g, b = parse_color(text_color)

function setup()
  print("Text Display loaded")
  led.clear()
  led.show()
end

function loop()
  local current_time = time.millis()
  led.clear()

  if scroll_mode == "static" then
    -- 静态显示
    local x_pos = 0
    if text_width <= 17 then
      x_pos = math.floor((17 - text_width) / 2)
    end
    led.text(x_pos, y_position, text, r, g, b)
  else
    -- 滚动显示
    if current_time - last_scroll_time >= scroll_speed then
      scroll_offset = scroll_offset + 1
      if scroll_offset > text_width + 17 then
        scroll_offset = 0
      end
      last_scroll_time = current_time
    end
    local x_pos = 17 - scroll_offset
    led.text(x_pos, y_position, text, r, g, b)
  end

  led.show()
  time.delay(30)
end

function unload()
  print("Text Display unloaded")
  led.clear()
  led.show()
end
```

### 示例 2：重力控制的粒子效果

```lua
-- @name: 重力粒子
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: Gravity-controlled particle effect
-- @id: particle

use("gravity")

local particles = {}
local particle_count = 20

function setup()
  print("Particle effect loaded")
  
  -- 初始化粒子
  for i = 1, particle_count do
    particles[i] = {
      x = math.random(0, 16),
      y = math.random(0, 7),
      vx = 0,
      vy = 0
    }
  end
  
  led.clear()
  led.show()
end

function loop()
  local gx, gy, gz, valid = gravity.get()
  
  if not valid then
    gx, gy = 0, 0
  end
  
  led.clear()
  
  -- 更新粒子
  for i = 1, particle_count do
    local p = particles[i]
    
    -- 应用重力
    p.vx = p.vx + gy * 0.5
    p.vy = p.vy + gx * 0.5
    
    -- 限制速度
    p.vx = math.max(-2, math.min(2, p.vx))
    p.vy = math.max(-2, math.min(2, p.vy))
    
    -- 更新位置
    p.x = p.x + p.vx
    p.y = p.y + p.vy
    
    -- 边界检测
    if p.x < 0 or p.x > 16 or p.y < 0 or p.y > 7 then
      p.x = math.random(0, 16)
      p.y = math.random(0, 7)
      p.vx = 0
      p.vy = 0
    end
    
    -- 绘制粒子
    local ix = math.floor(p.x + 0.5)
    local iy = math.floor(p.y + 0.5)
    if ix >= 0 and ix <= 16 and iy >= 0 and iy <= 7 then
      led.set(ix, iy, 0, 255, 255)
    end
  end
  
  led.show()
  time.delay(50)
end

function unload()
  print("Particle effect unloaded")
  led.clear()
  led.show()
end
```

### 示例 3：音频频谱可视化

```lua
-- @name: 频谱显示
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: Audio spectrum visualizer
-- @id: spectrum

use("audio")

local bars = {}
local smoothing = 0.3

function setup()
  print("Spectrum visualizer loaded")
  
  -- 初始化频谱条
  for i = 0, 16 do
    bars[i] = 0
  end
  
  audio.init()
  led.clear()
  led.show()
end

function loop()
  local spectrum = audio.getSpectrum()
  
  if spectrum then
    -- 平滑处理
    for i = 0, 16 do
      local target = spectrum[i] or 0
      bars[i] = bars[i] * (1 - smoothing) + target * smoothing
    end
  end
  
  led.clear()
  
  -- 绘制频谱条
  for x = 0, 16 do
    local height = math.floor(bars[x] * 8)
    height = math.min(height, 7)
    
    for y = 0, height do
      -- 彩虹色
      local hue = (x * 15 + time.millis() / 50) % 255
      local r, g, b = led.hsv(hue, 255, 255)
      led.set(x, 7 - y, r, g, b)
    end
  end
  
  led.show()
  time.delay(30)
end

function unload()
  print("Spectrum visualizer unloaded")
  audio.close()
  led.clear()
  led.show()
end
```

---

## 技术限制

### 硬件限制
- LED 矩阵：17x8 像素
- 内存：有限，避免创建大量对象
- CPU：ESP32，避免复杂计算

### 软件限制
- Lua 版本：5.4.7
- 模块 ID：最长 8 个字符（NVS 键名限制）
- 配置键名：`cfg_<module_id>` 总长度不超过 15 个字符
- 不支持文件 I/O
- 不支持网络操作
- 不支持多线程

### API 限制
- `led.text()` 仅支持 ASCII 0x20-0x7A
- `gravity.get()` 数据可能不可用（检查 `valid`）
- `audio.getSpectrum()` 返回 17 个频段（0-16）

---

## 故障排查

### 模块无法加载
1. 检查元数据格式是否正确
2. 确认模块 ID 不超过 8 个字符
3. 检查 Lua 语法错误（使用 `print` 调试）

### 配置不生效
1. 确认配置定义已上传到云数据库
2. 检查 `CONFIG` 表是否正确读取
3. 验证配置键名与定义一致

### LED 不显示
1. 确认调用了 `led.show()`
2. 检查坐标是否在范围内（x: 0-16, y: 0-7）
3. 验证颜色值是否正确（0-255）

### 性能问题
1. 减少 `loop()` 中的计算量
2. 增加 `time.delay()` 延迟
3. 使用局部变量和缓存
4. 避免频繁创建对象

---

## 更多资源

- 查看内置模块源码：`cloudfunctions/initDatabase/index.js`
- 硬件 API 实现：`C:\Users\jeff\Desktop\Bottle\src\lua_hardware_api.cpp`
- 配置系统：`C:\Users\jeff\Desktop\Bottle\src\module_registry.cpp`

---

**版本**：1.0.0  
**更新日期**：2026-05-11
