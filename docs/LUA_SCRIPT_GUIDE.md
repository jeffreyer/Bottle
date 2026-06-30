# Lua 动画开发指南

本文档为 AI 助手和开发者提供快速开发 Lua 动画模块的完整指南。

## 目录
- [硬件环境](#硬件环境)
- [文件结构](#文件结构)
- [核心 API](#核心-api)
- [动画开发模式](#动画开发模式)
- [常见问题](#常见问题)
- [最佳实践](#最佳实践)

---

## 硬件环境
有两个硬件版本，核心区别：长版LED矩阵32×8，短版LED矩阵17×8，在开发时需要兼容两个版本，尽量同一份代码适配两个版本，如无法适配可为两个版本单独开发；长版在连接小程序时应答元数据model=Bottle-V4，短版应答元数据model=Bottle-V1。
### LED 矩阵
- **尺寸**: 长版：32 宽 × 8 高 (`MATRIX_WIDTH = 32`, `MATRIX_HEIGHT = 8`)；短版：17 宽 × 8 高 (`MATRIX_WIDTH = 17`, `MATRIX_HEIGHT = 8`)
- **全局变量**: `WIDTH` 和 `HEIGHT` 由设备端注册，**不要硬编码**
- **坐标系**: 长版：左下角为 (0, 0)，右上角为 (31, 7)；短版：左下角为 (0, 0)，右上角为 (16, 7)

### 传感器
- **加速度计**: 提供重力感应，**注意 x-y 轴需要对调**
- **麦克风**: 提供 FFT 音频数据，512 个频段
- **按键**: 支持单击和长按事件

---

## 文件结构

### 必需的文件头元数据
```lua
-- @name: 模块名称
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: 中文描述（必须使用中文）
-- @id: module-id-kebab-case
```

**重要**: 
- `@id` 和文件名**不要使用下划线 `_`**，使用连字符 `-` 代替
- 下划线是内部分隔符，会导致解析错误
- ✅ 正确：`wooden-fish.lua`, `@id: wooden-fish`
- ❌ 错误：`wooden_fish.lua`, `@id: wooden_fish`

**字段长度限制**：
- `@description` 字段**必须控制在 128 字节以内**
- 中文字符通常占 3 字节（UTF-8 编码）
- **建议不超过 42 个中文字符**（约 126 字节）
- 超过限制会导致 BLE 传输时被截断，出现乱码

### 标准三函数结构
#### `setup()`
- 模块加载时调用一次
- 用于初始化状态、加载配置、设置硬件
- 可选函数

#### `loop()`
- 主循环，持续调用
- 实现模块的主要逻辑
- 必需函数

#### `unload()`
- 模块卸载时调用
- 用于清理资源、保存状态
- 可选函数

### 读取配置
- 配置通过全局 `CONFIG` 表访问

```lua
use("button")  -- 声明使用的硬件资源
use("gravity")
use("audio")

-- 配置参数（正确处理布尔值）
local my_param = CONFIG.my_param or 10
local my_bool = true
if CONFIG.my_bool ~= nil then
  my_bool = CONFIG.my_bool
end

-- 状态变量
local animation_progress = 0.0
local last_frame_time = 0

function setup()
  print("Module loaded")
  last_frame_time = time.millis()
end

function loop()
  local current_time = time.millis()
  local dt = (current_time - last_frame_time) / 1000.0
  last_frame_time = current_time
  
  -- 更新逻辑
  -- 绘制逻辑
  
  led.show()
  time.delay(16)  -- ~60 FPS
end

function unload()
  print("Module unloaded")
  led.clear()
  led.show()
end
```

---

## 核心 API

### LED 控制

#### `led.set(x, y, r, g, b)`
设置单个像素的 RGB 颜色。
```lua
led.set(0, 0, 255, 0, 0)  -- 左上角设为红色
```

#### `led.hsv(h, s, v) -> r, g, b`
**颜色转换函数**，返回 RGB 值（不是直接设置像素）。
```lua
-- 错误用法
led.hsv(x, y, 120, 255, 200)  -- ❌ 参数错误

-- 正确用法
local r, g, b = led.hsv(120, 255, 200)  -- ✅ 转换颜色
led.set(x, y, r, g, b)  -- ✅ 设置像素
```

**为什么用 HSV？**
- **H (色相)**: 通过配置改变整体颜色风格
- **S (饱和度)**: 控制颜色鲜艳程度
- **V (亮度)**: 方便实现渐变、脉动、淡入淡出

#### `led.text(x, y, text, r, g, b)`
绘制文本（3×5 字体）。
```lua
led.text(10, 2, "99", 255, 255, 0)  -- 黄色文字
```

**字体特性**：
- 3x5 像素字体
- 支持大写字母 A-Z
- 支持小写字母 a-z（较小）
- 支持数字 0-9
- 支持常用符号（空格、标点、运算符等）
- 字符间距：4 像素（3 像素字符 + 1 像素间距）

#### `led.palette(palette_table, index, brightness) -> r, g, b`
从调色板插值获取颜色。

**参数**：
- `palette_table`: 调色板表，每个条目格式为 `{position, r, g, b}`
- `index`: 颜色索引 (0-255)
- `brightness`: 亮度 (0-255，可选，默认 255)

**返回**：`r, g, b` (0-255)

**工作原理**：
- 在调色板中找到 index 两侧的颜色条目
- 使用线性插值计算中间颜色
- 应用亮度调整

```lua
-- 定义彩虹调色板
local rainbow_palette = {
  {0,   255, 0,   0},    -- 红色
  {85,  255, 255, 0},    -- 黄色
  {170, 0,   255, 0},    -- 绿色
  {255, 0,   0,   255}   -- 蓝色
}

-- 获取索引 128 处的颜色（绿色和黄色之间）
local r, g, b = led.palette(rainbow_palette, 128, 255)
led.set(0, 0, r, g, b)

-- 使用调色板创建渐变效果
for x = 0, 16 do
  local index = math.floor((x / 16) * 255)
  local r, g, b = led.palette(rainbow_palette, index, 200)
  led.set(x, 4, r, g, b)
end
```

**使用场景**：
- 创建平滑的颜色渐变
- 实现彩虹效果
- 音频可视化的颜色映射
- 热力图显示

#### `led.clear()` / `led.show()`
清空缓冲区 / 刷新显示。

### 音频 FFT

#### `fft.get(index) -> magnitude`
获取指定频段的幅度值（**原始值很大，需要归一化**）。
```lua
local value = fft.get(10)
value = value / 10000.0  -- ✅ 归一化到 0-1 范围
```

#### `fft.count() -> 512`
返回 FFT 频段数量（固定 512）。

**频段映射**（采样率 16kHz）：
- 0-10: 低音 (0-100Hz)
- 10-200: 中音 (100-2000Hz)
- 200-400: 高音 (2000-4000Hz)

**重要**: 所有使用 FFT 的代码必须归一化，否则会导致显示异常。

### 重力感应

#### `gravity.get() -> gx, gy, gz, valid`
获取加速度计数据。

**关键**: **必须对调 x-y 轴**
```lua
local gx, gy, gz, valid = gravity.get()
if valid then
  gx, gy = gy, gx  -- ✅ 对调 x-y 轴
  -- 使用 gx, gy, gz
end
```

### 按键

#### `button.poll() -> event_type`
返回按键事件：
- `0`: 无事件
- `1`: 单击
- `2`: 长按

```lua
local event = button.poll()
if event > 0 then  -- 单击或长按
  trigger_action()
end
```

#### `button.is_holding() -> boolean`
检测按键是否被按住（用于持续检测，不是事件）。

### 时间

#### `time.millis() -> milliseconds`
返回启动后的毫秒数。

#### `time.delay(ms)`
阻塞延迟（通常用 16ms 实现 60 FPS）。

---

## 动画开发模式

### 模式 1: 基于时间的动画

使用独立的帧时间计算，避免时间重置问题。

```lua
local animation_progress = 0.0
local last_frame_time = 0

function setup()
  last_frame_time = time.millis()
end

function loop()
  local current_time = time.millis()
  local dt = (current_time - last_frame_time) / 1000.0
  last_frame_time = current_time
  
  -- 更新动画
  if animation_progress > 0.0 then
    animation_progress = animation_progress - dt * 2.0  -- 衰减速度
    if animation_progress < 0.0 then
      animation_progress = 0.0
    end
  end
  
  -- 绘制
  draw_with_animation(animation_progress)
end
```

**关键点**:
- 使用 `last_frame_time` 而不是事件时间
- `dt` 是帧间隔（秒），用于平滑动画
- 每帧更新 `last_frame_time`

### 模式 2: 缩放动画

```lua
local function draw_scaled_object(scale)
  -- scale: 1.0 = 正常, 0.8 = 缩小到 80%
  
  for y = 0, HEIGHT - 1 do
    for x = 0, WIDTH - 1 do
      -- 计算相对于中心的位置
      local cx = WIDTH / 2
      local cy = HEIGHT / 2
      local dx = x - cx
      local dy = y - cy
      
      -- 应用缩放
      local sx = cx + dx * scale
      local sy = cy + dy * scale
      
      -- 判断原始位置是否应该绘制
      local ix = math.floor(sx + 0.5)
      local iy = math.floor(sy + 0.5)
      
      if should_draw_at(ix, iy) then
        led.set(x, y, r, g, b)
      end
    end
  end
end
```

### 模式 3: 简化缩放（跳过像素）

```lua
local function draw_with_shrink(shrink)
  local function set_pixel(x, y, r, g, b)
    if shrink then
      -- 跳过边缘像素实现缩小效果
      if x == 0 or x == WIDTH-1 or y == 0 or y == HEIGHT-1 then
        return
      end
    end
    led.set(x, y, r, g, b)
  end
  
  -- 使用 set_pixel 绘制所有像素
  set_pixel(2, 3, 255, 0, 0)
end
```

### 模式 4: 颜色渐变

```lua
local function draw_gradient(progress)
  -- progress: 0.0 -> 1.0
  
  for x = 0, WIDTH - 1 do
    local intensity = (x / WIDTH) * progress
    local brightness = math.floor(intensity * 255)
    local r, g, b = led.hsv(120, 255, brightness)
    led.set(x, 4, r, g, b)
  end
end
```

---

## 常见问题

### 问题 1: 硬编码 WIDTH/HEIGHT

❌ **错误**:
```lua
local WIDTH = 16
local HEIGHT = 16
```

✅ **正确**:
```lua
-- WIDTH 和 HEIGHT 是全局变量，直接使用
local CENTER_X = WIDTH / 2
local CENTER_Y = HEIGHT / 2
```

### 问题 2: 错误使用 led.hsv

❌ **错误**:
```lua
led.hsv(x, y, 120, 255, 200)  -- 参数错误
```

✅ **正确**:
```lua
local r, g, b = led.hsv(120, 255, 200)
led.set(x, y, r, g, b)
```

### 问题 3: 布尔值配置错误

❌ **错误**:
```lua
local my_bool = CONFIG.my_bool or true
-- 问题：当 CONFIG.my_bool = false 时，结果仍为 true
```

✅ **正确**:
```lua
local my_bool = true
if CONFIG.my_bool ~= nil then
  my_bool = CONFIG.my_bool
end
```

### 问题 4: FFT 未归一化

❌ **错误**:
```lua
local value = fft.get(10)
-- value 可能是几千，导致显示异常
```

✅ **正确**:
```lua
local value = fft.get(10)
value = value / 10000.0  -- 归一化
```

### 问题 5: 重力轴向错误

❌ **错误**:
```lua
local gx, gy, gz, valid = gravity.get()
-- 直接使用 gx, gy
```

✅ **正确**:
```lua
local gx, gy, gz, valid = gravity.get()
if valid then
  gx, gy = gy, gx  -- 对调 x-y 轴
  -- 使用对调后的值
end
```

---

## 最佳实践

### 1. 配置文件编写指南

#### 配置文件格式

创建 `[module-id].cfg`（JSON 数组格式）：

```json
[
  {
    "key": "main_color",
    "type": "color",
    "label": "主要颜色",
    "default": "#FF8800",
    "desc": "动画的主要颜色"
  },
  {
    "key": "speed",
    "type": "slider",
    "label": "动画速度",
    "min": 0.5,
    "max": 5.0,
    "step": 0.1,
    "default": 2.0,
    "desc": "动画播放速度"
  },
  {
    "key": "enable_gravity",
    "type": "switch",
    "label": "启用重力感应",
    "default": true,
    "desc": "是否响应设备倾斜"
  }
]
```

#### 配置类型详解

##### 1. color - 颜色选择器

```json
{
  "key": "main_color",
  "type": "color",
  "label": "主要颜色",
  "default": "#FF8800",
  "desc": "动画的主要颜色"
}
```

- **值类型**: 字符串，格式 `"#RRGGBB"`
- **用途**: 提供颜色选择器界面
- **Lua 读取**:
  ```lua
  local color_str = CONFIG.main_color or "#FF8800"
  local function parse_color(hex)
    hex = hex:gsub("#", "")
    local r = tonumber(hex:sub(1, 2), 16)
    local g = tonumber(hex:sub(3, 4), 16)
    local b = tonumber(hex:sub(5, 6), 16)
    return r, g, b
  end
  local r, g, b = parse_color(color_str)
  ```

##### 2. slider - 滑块选择

```json
{
  "key": "speed",
  "type": "slider",
  "label": "速度",
  "min": 0.5,
  "max": 5.0,
  "step": 0.1,
  "default": 2.0,
  "desc": "动画播放速度"
}
```

- **值类型**: 数字
- **必需字段**: `min`, `max`, `step`
- **可选字段**: `unit` (单位，如 "ms", "%")
- **Lua 读取**:
  ```lua
  local speed = CONFIG.speed or 2.0
  ```

##### 3. switch - 开关

```json
{
  "key": "enable_feature",
  "type": "switch",
  "label": "启用功能",
  "default": true,
  "desc": "是否启用该功能"
}
```

- **值类型**: 布尔值 (true/false)
- **Lua 读取** (正确处理):
  ```lua
  local enable_feature = true
  if CONFIG.enable_feature ~= nil then
    enable_feature = CONFIG.enable_feature
  end
  ```
- **注意**: 不要使用 `or` 运算符，会导致 false 被误判为 true

##### 4. select - 下拉选择

```json
{
  "key": "mode",
  "type": "select",
  "label": "显示模式",
  "default": "auto",
  "options": [
    {"value": "static", "label": "静态"},
    {"value": "auto", "label": "自动"},
    {"value": "scroll", "label": "滚动"}
  ],
  "desc": "选择显示模式"
}
```

- **值类型**: 字符串或数字
- **必需字段**: `options` 数组
- **options 格式**: 每个选项包含 `value` 和 `label`
- **Lua 读取**:
  ```lua
  local mode = CONFIG.mode or "auto"
  
  if mode == "static" then
    -- 静态模式逻辑
  elseif mode == "auto" then
    -- 自动模式逻辑
  elseif mode == "scroll" then
    -- 滚动模式逻辑
  end
  ```

**数字值的 select 示例**:
```json
{
  "key": "pattern",
  "type": "select",
  "label": "图案",
  "default": 0,
  "options": [
    {"value": 0, "label": "圆形"},
    {"value": 1, "label": "方形"},
    {"value": 2, "label": "三角形"}
  ],
  "desc": "选择图案类型"
}
```

```lua
local pattern = CONFIG.pattern or 0

if pattern == 0 then
  draw_circle()
elseif pattern == 1 then
  draw_square()
elseif pattern == 2 then
  draw_triangle()
end
```

##### 5. text - 文本输入

```json
{
  "key": "display_text",
  "type": "text",
  "label": "显示文字",
  "default": "Hello",
  "desc": "要显示的文本内容"
}
```

- **值类型**: 字符串
- **Lua 读取**:
  ```lua
  local text = CONFIG.display_text or "Hello"
  ```

##### 6. number - 数字输入

```json
{
  "key": "count",
  "type": "number",
  "label": "数量",
  "default": 10,
  "desc": "粒子数量"
}
```

- **值类型**: 数字
- **Lua 读取**:
  ```lua
  local count = CONFIG.count or 10
  ```

#### 配置最佳实践

**1. 使用 desc 而不是 description**
```json
// ✅ 正确
{"desc": "这是描述"}

// ❌ 错误
{"description": "这是描述"}
```

**2. 颜色配置使用 color 类型**
```json
// ✅ 正确 - 提供颜色选择器
{"type": "color", "default": "#FF8800"}

// ❌ 错误 - 用户体验差
{"type": "slider", "min": 0, "max": 255, "label": "色相"}
```

**3. 布尔值正确处理**
```lua
// ❌ 错误 - false 会被当作 true
local my_bool = CONFIG.my_bool or true

// ✅ 正确
local my_bool = true
if CONFIG.my_bool ~= nil then
  my_bool = CONFIG.my_bool
end
```

**4. Key长度不超过15个字符**
受限于NVS存储key长度限制，所有配置项key长度限制最大为 15 个字符

**5. 提供合理的默认值**
- 确保模块在没有配置时也能正常运行
- 默认值应该是最常用的选项

**6. 描述要清晰**
- 简洁说明配置项的作用
- 必要时说明取值范围或单位

#### 完整配置示例

```json
[
  {
    "key": "main_color",
    "type": "color",
    "label": "主要颜色",
    "default": "#FF8800",
    "desc": "动画的主要颜色"
  },
  {
    "key": "speed",
    "type": "slider",
    "label": "速度",
    "min": 0.5,
    "max": 5.0,
    "step": 0.1,
    "default": 2.0,
    "unit": "倍",
    "desc": "动画播放速度"
  },
  {
    "key": "mode",
    "type": "select",
    "label": "模式",
    "default": "auto",
    "options": [
      {"value": "static", "label": "静态"},
      {"value": "auto", "label": "自动"},
      {"value": "scroll", "label": "滚动"}
    ],
    "desc": "显示模式"
  },
  {
    "key": "enable_gravity",
    "type": "switch",
    "label": "重力感应",
    "default": true,
    "desc": "是否启用重力感应"
  },
  {
    "key": "display_text",
    "type": "text",
    "label": "显示文字",
    "default": "Hello",
    "desc": "要显示的文本"
  },
  {
    "key": "particle_count",
    "type": "number",
    "label": "粒子数量",
    "default": 20,
    "desc": "粒子效果的数量"
  }
]
```

对应的 Lua 读取代码：

```lua
-- 颜色配置
local color_str = CONFIG.main_color or "#FF8800"
local function parse_color(hex)
  hex = hex:gsub("#", "")
  local r = tonumber(hex:sub(1, 2), 16) or 255
  local g = tonumber(hex:sub(3, 4), 16) or 136
  local b = tonumber(hex:sub(5, 6), 16) or 40
  return r, g, b
end
local r, g, b = parse_color(color_str)

-- 数字配置
local speed = CONFIG.speed or 2.0
local particle_count = CONFIG.particle_count or 20

-- 字符串配置
local mode = CONFIG.mode or "auto"
local display_text = CONFIG.display_text or "Hello"

-- 布尔值配置
local enable_gravity = true
if CONFIG.enable_gravity ~= nil then
  enable_gravity = CONFIG.enable_gravity
end
```

### 2. 性能优化

- **避免重复计算**: 在循环外计算颜色
  ```lua
  -- ❌ 每次循环都转换
  for i = 0, 10 do
    local r, g, b = led.hsv(120, 255, 200)
    led.set(i, 0, r, g, b)
  end
  
  -- ✅ 只转换一次
  local r, g, b = led.hsv(120, 255, 200)
  for i = 0, 10 do
    led.set(i, 0, r, g, b)
  end
  ```

- **限制帧率**: 使用 `time.delay(16)` 实现 60 FPS

### 3. 布局设计

对于 17×8 的矩阵，常见布局：
- **左侧 8×8**: 主要内容区域（正方形）
- **右侧 9×8**: 状态信息、文字显示

```lua
-- 左侧：主要动画（0-7）
for x = 0, 7 do
  draw_main_content(x, y)
end

-- 右侧：状态显示（9-16）
led.text(10, 2, "99", 255, 255, 0)
```

### 4. 调试技巧

使用 `print()` 输出调试信息：
```lua
print("Animation progress: " .. animation_progress)
```

输出会显示在设备端串口日志中。

### 5. 资源声明

在文件开头声明使用的硬件资源：
```lua
use("button")   -- 按键
use("gravity")  -- 重力感应
use("audio")    -- 音频/FFT
```

---

## 完整示例：简单脉动动画

```lua
-- @name: 脉动圆点
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: 中心圆点脉动动画
-- @id: pulse-dot

use("button")

-- 配置
local pulse_speed = CONFIG.pulse_speed or 2.0
local color_hue = CONFIG.color_hue or 120

-- 状态
local pulse_phase = 0.0
local last_frame_time = 0

function setup()
  print("Pulse Dot loaded")
  last_frame_time = time.millis()
end

function loop()
  local current_time = time.millis()
  local dt = (current_time - last_frame_time) / 1000.0
  last_frame_time = current_time
  
  -- 更新脉动相位
  pulse_phase = pulse_phase + dt * pulse_speed
  if pulse_phase > 1.0 then
    pulse_phase = pulse_phase - 1.0
  end
  
  -- 计算亮度（正弦波）
  local brightness = (math.sin(pulse_phase * math.pi * 2) + 1) / 2
  brightness = math.floor(brightness * 255)
  
  -- 绘制
  led.clear()
  
  local cx = math.floor(WIDTH / 2)
  local cy = math.floor(HEIGHT / 2)
  local r, g, b = led.hsv(color_hue, 255, brightness)
  
  -- 中心点
  led.set(cx, cy, r, g, b)
  
  -- 周围点（亮度减半）
  local r2, g2, b2 = led.hsv(color_hue, 255, math.floor(brightness * 0.5))
  led.set(cx - 1, cy, r2, g2, b2)
  led.set(cx + 1, cy, r2, g2, b2)
  led.set(cx, cy - 1, r2, g2, b2)
  led.set(cx, cy + 1, r2, g2, b2)
  
  led.show()
  time.delay(16)
end

function unload()
  print("Pulse Dot unloaded")
  led.clear()
  led.show()
end
```

---

## 快速检查清单

开发新模块时，确保：

- [ ] 文件头包含完整元数据（中文 @description）
- [ ] **文件名和 @id 使用连字符 `-`，不使用下划线 `_`**
- [ ] **颜色配置使用 `type: "color"`**
- [ ] **配置项描述使用 `"desc"`，不是 `"description"`**
- [ ] 使用 `setup() / loop() / unload()` 结构
- [ ] 不硬编码 WIDTH/HEIGHT
- [ ] 正确使用 `led.hsv()` 返回值
- [ ] 布尔值配置使用 `if CONFIG.xxx ~= nil then`
- [ ] FFT 数据归一化（除以 10000.0）
- [ ] 重力数据 x-y 轴对调
- [ ] 使用独立的 `last_frame_time` 计算 dt
- [ ] 按键使用 `button.poll()` 返回值
- [ ] 帧率控制 `time.delay(16)`

---

## 更多示例模块

### 示例：文字滚动显示

```lua
-- @name: 文字显示
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: 自定义文字滚动显示
-- @id: text-show

-- 配置
local text = CONFIG.text or "Hello"
local text_color = CONFIG.text_color or "#00FF00"
local scroll_speed = CONFIG.scroll_speed or 100
local scroll_mode = CONFIG.scroll_mode or "auto"
local y_position = CONFIG.y_position or 1

-- 解析颜色
local function parse_color(hex)
  if type(hex) ~= "string" then return 0, 255, 0 end
  hex = hex:gsub("#", "")
  if #hex ~= 6 then return 0, 255, 0 end
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

---

## 内存管理最佳实践

### 避免频繁创建表

```lua
-- ❌ 错误：每帧创建新表
function loop()
  local temp_table = {}  -- 增加 GC 压力
  -- 使用 temp_table
end

-- ✅ 正确：复用表
local temp_table = {}
function loop()
  -- 清空并复用
  for k in pairs(temp_table) do
    temp_table[k] = nil
  end
  -- 使用 temp_table
end
```

### 预分配数组

```lua
-- ✅ 好的做法
local particles = {}
for i = 1, 100 do
  particles[i] = {x = 0, y = 0, vx = 0, vy = 0}
end

-- ❌ 避免在循环中动态增长
```

---

## 代码组织最佳实践

### 使用模块化函数

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

### 使用常量

```lua
-- 在文件顶部定义常量
local MAX_PARTICLES = 50
local UPDATE_INTERVAL = 100

-- 使用常量
for x = 0, WIDTH - 1 do
  for y = 0, HEIGHT - 1 do
    -- ...
  end
end
```

---

## 错误处理

### 检查配置值

```lua
local speed = CONFIG.speed or 100
if speed < 10 then speed = 10 end
if speed > 1000 then speed = 1000 end
```

### 验证硬件数据

```lua
local gx, gy, gz, valid = gravity.get()
if not valid then
  -- 使用默认值或跳过
  return
end
```

---

## 调试技巧

### 使用 print 输出

```lua
function setup()
  print("Module loaded")
  print("Config value:", CONFIG.speed or "not set")
end

function loop()
  local gx, gy = gravity.get()
  if time.millis() % 1000 == 0 then
    print("Gravity:", gx, gy)
  end
end
```

输出会显示在设备端串口监视器中。

---

## 技术限制

### 硬件限制
- LED 矩阵：17×8 像素
- 内存：有限，避免创建大量对象
- CPU：ESP32，避免复杂计算

### 软件限制
- Lua 版本：5.4.7
- 不支持文件 I/O
- 不支持网络操作
- 不支持多线程

### API 限制
- `led.text()` 仅支持 ASCII 0x20-0x7A
- `gravity.get()` 数据可能不可用（检查 `valid`）
- `fft.count()` 返回 512 个频段

---

## 故障排查

### 模块无法加载
1. 检查元数据格式是否正确
2. 确认文件名和 @id 使用连字符
3. 检查 Lua 语法错误（使用 `print` 调试）

### 配置不生效
1. 确认配置文件已上传
2. 检查 `CONFIG` 表是否正确读取
3. 验证配置键名与定义一致
4. 确认使用 `"desc"` 而不是 `"description"`

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

**版本**: 2.0.0  
**更新时间**: 2026-05-29  
**适用设备**: Bottle (ESP32 + 17×8 LED 矩阵)
