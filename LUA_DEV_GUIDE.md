# Bottle 设备 Lua 开发指南

## 目录
- [简介](#简介)
- [开发环境准备](#开发环境准备)
- [Lua API 参考](#lua-api-参考)
- [创建你的第一个模块](#创建你的第一个模块)
- [调色盘系统](#调色盘系统)
- [最佳实践](#最佳实践)
- [调试技巧](#调试技巧)
- [示例代码](#示例代码)

---

## 简介

Bottle 设备支持使用 Lua 脚本开发自定义的 LED 矩阵动画效果。通过 Lua，你可以：

- 控制 17x8 LED 矩阵显示
- 读取音频频谱数据（FFT）
- 获取重力传感器数据
- 使用丰富的调色盘系统
- 创建时间驱动的动画效果

**硬件规格：**
- LED 矩阵：17x8 像素（WIDTH=17, HEIGHT=8）
- 音频输入：PDM 麦克风，16kHz 采样率
- FFT 分辨率：512 bins
- 重力传感器：MPU6050 三轴加速度计
- Lua 版本：Lua 5.1

---

## 开发环境准备

### 1. 安装 PlatformIO

```bash
# 使用 VS Code 安装 PlatformIO 扩展
# 或使用命令行安装
pip install platformio
```

### 2. 克隆项目

```bash
git clone <your-bottle-repo>
cd Bottle
```

### 3. 项目结构

```
Bottle/
├── include/           # 头文件
│   ├── lua_hardware_api.h
│   └── your_module.h
├── src/              # 源文件
│   ├── lua_hardware_api.cpp
│   ├── module_registry.cpp
│   └── your_module.cpp
└── platformio.ini    # 构建配置
```

---

## Lua API 参考

### LED 控制 API

#### `led.clear()`
清空所有 LED。

```lua
led.clear()
```

#### `led.show()`
将缓冲区内容刷新到 LED 硬件。

```lua
led.show()
```

#### `led.set(x, y, r, g, b)`
设置指定位置的 LED 颜色。

**参数：**
- `x`: 横坐标 (0 到 WIDTH-1)
- `y`: 纵坐标 (0 到 HEIGHT-1)
- `r`: 红色分量 (0-255)
- `g`: 绿色分量 (0-255)
- `b`: 蓝色分量 (0-255)

```lua
-- 设置 (5, 3) 位置为红色
led.set(5, 3, 255, 0, 0)
```

#### `led.hsv(h, s, v) -> r, g, b`
将 HSV 颜色转换为 RGB。

**参数：**
- `h`: 色调 (0-255)
- `s`: 饱和度 (0-255)
- `v`: 明度 (0-255)

**返回：** `r, g, b` 三个值

```lua
local r, g, b = led.hsv(128, 255, 180)
led.set(0, 0, r, g, b)
```

#### `led.palette(palette_table, index, brightness) -> r, g, b`
从调色盘中获取插值颜色。

**参数：**
- `palette_table`: 调色盘表 `{{index, r, g, b}, ...}`
- `index`: 颜色索引 (0-255)
- `brightness`: 亮度 (0-255，可选，默认 255)

**返回：** `r, g, b` 三个值

```lua
local rainbow = {
  {0, 255, 0, 0},      -- 红
  {127, 0, 255, 0},    -- 绿
  {255, 0, 0, 255}     -- 蓝
}
local r, g, b = led.palette(rainbow, 64, 180)
```

---

### FFT 音频 API

#### `fft.get(index) -> magnitude`
获取指定 FFT bin 的幅度值。

**参数：**
- `index`: FFT bin 索引 (0-511)

**返回：** 幅度值（浮点数）

```lua
local low_freq = fft.get(10)   -- 低频
local mid_freq = fft.get(50)   -- 中频
local high_freq = fft.get(200) -- 高频
```

#### `fft.count() -> count`
获取 FFT bin 总数（固定为 512）。

```lua
local total_bins = fft.count()  -- 返回 512
```

**频率计算：**
```lua
-- 采样率 16000 Hz，FFT 长度 512
-- 每个 bin 的频率间隔 = 16000 / 512 = 31.25 Hz
-- bin N 的频率 = N * 31.25 Hz
```

---

### 重力传感器 API

#### `gravity.get() -> x, y, z, valid`
获取重力传感器数据。

**返回：**
- `x`: X 轴加速度 (-1.0 到 1.0)
- `y`: Y 轴加速度 (-1.0 到 1.0)
- `z`: Z 轴加速度 (-1.0 到 1.0)
- `valid`: 数据是否有效（布尔值）

```lua
local gx, gy, gz, valid = gravity.get()
if valid then
  if gx > 0.7 then
    -- 设备向右倾斜
  elseif gx < -0.7 then
    -- 设备向左倾斜
  end
end
```

---

### 时间 API

#### `time.millis() -> milliseconds`
获取系统启动后的毫秒数。

```lua
local now = time.millis()
```

---

### 配置 API

#### `config.get(key) -> value`
读取配置值。

**参数：**
- `key`: 配置键名（字符串）

**返回：** 整数值

```lua
local brightness = config.get("brightness")
```

---

### 全局常量

- `WIDTH`: 矩阵宽度（17）
- `HEIGHT`: 矩阵高度（8）

```lua
for x = 0, WIDTH - 1 do
  for y = 0, HEIGHT - 1 do
    led.set(x, y, 255, 0, 0)
  end
end
```

---

## 创建你的第一个模块

### 步骤 1：创建头文件

创建 `include/mymodule_lua.h`：

```cpp
#pragma once

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

int setup_mymodule_lua_module(void);
int unload_mymodule_lua_module(void);
int loop_mymodule_lua_module(void);

#ifdef __cplusplus
}
String mymodule_lua_module_runtime_status_json(void);
String mymodule_lua_module_configs_json(void);
#endif
```

### 步骤 2：创建实现文件

创建 `src/mymodule_lua.cpp`：

```cpp
#include "mymodule_lua.h"

#include <Arduino.h>
#include <FastLED.h>
#include "app_control.h"
#include "common.h"
#include "gravity.h"
#include "lua_hardware_api.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace {

const char* kDefaultConfigKey = "mymodule_style";

struct MyModuleLuaHost {
  lua_State* L = nullptr;
  int saved_subpage = -1;
  bool script_loaded = false;
};

MyModuleLuaHost g_host;

// 你的 Lua 脚本
const char* kDefaultLuaScript = R"lua(
-- 在这里编写你的 Lua 代码

function setup()
  led.clear()
  led.show()
end

function loop()
  led.clear()
  
  -- 你的动画逻辑
  for x = 0, WIDTH - 1 do
    for y = 0, HEIGHT - 1 do
      local r, g, b = led.hsv((x + y) * 10, 255, 100)
      led.set(x, y, r, g, b)
    end
  end
  
  led.show()
end

function unload()
  led.clear()
  led.show()
end
)lua";

}  // namespace

int setup_mymodule_lua_module(void) {
  // 初始化重力传感器（如果需要）
  gravity_init();
  int err = gravity_sensor_start();
  if (err != 0) {
    Serial.println("[mymodule_lua] Gravity sensor start failed");
  }

  // 设置亮度
  brightness_max = 30;
  FastLED.setBrightness(30);

  // 加载配置
  if (g_host.saved_subpage < 0) {
    g_host.saved_subpage = load_config(kDefaultConfigKey);
  }
  subpage_index = g_host.saved_subpage;

  // 初始化 Lua
  Serial.println("[mymodule_lua] Initializing Lua...");
  g_host.L = luaL_newstate();
  if (!g_host.L) {
    Serial.println("[mymodule_lua] ERROR: Failed to create Lua state");
    return -1;
  }

  // 打开标准库
  luaL_openlibs(g_host.L);

  // 注册硬件 API
  register_lua_hardware_apis(g_host.L);

  // 注册 sys.page_index() 函数（用于切换子页面）
  lua_newtable(g_host.L);
  lua_pushcfunction(g_host.L, [](lua_State* L) -> int {
    lua_pushnumber(L, subpage_index);
    return 1;
  });
  lua_setfield(g_host.L, -2, "page_index");
  lua_setglobal(g_host.L, "sys");

  // 加载并执行脚本
  Serial.println("[mymodule_lua] Loading Lua script...");
  if (luaL_dostring(g_host.L, kDefaultLuaScript) != LUA_OK) {
    const char* error = lua_tostring(g_host.L, -1);
    Serial.printf("[mymodule_lua] ERROR: %s\n", error);
    lua_close(g_host.L);
    g_host.L = nullptr;
    return -2;
  }

  g_host.script_loaded = true;
  Serial.println("[mymodule_lua] Lua script loaded successfully");

  // 调用 setup 函数
  lua_getglobal(g_host.L, "setup");
  if (lua_isfunction(g_host.L, -1)) {
    if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(g_host.L, -1);
      Serial.printf("[mymodule_lua] ERROR in setup: %s\n", error);
      lua_pop(g_host.L, 1);
    }
  } else {
    lua_pop(g_host.L, 1);
  }

  return 0;
}

int unload_mymodule_lua_module(void) {
  g_host.saved_subpage = subpage_index;
  save_config(kDefaultConfigKey, subpage_index % 4);

  // 调用 unload 函数
  if (g_host.L && g_host.script_loaded) {
    lua_getglobal(g_host.L, "unload");
    if (lua_isfunction(g_host.L, -1)) {
      if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(g_host.L, -1);
        Serial.printf("[mymodule_lua] ERROR in unload: %s\n", error);
        lua_pop(g_host.L, 1);
      }
    } else {
      lua_pop(g_host.L, 1);
    }
  }

  // 关闭 Lua 状态
  if (g_host.L) {
    lua_close(g_host.L);
    g_host.L = nullptr;
  }
  g_host.script_loaded = false;

  // 停止重力传感器
  gravity_sensor_sleep();

  Serial.println("[mymodule_lua] Module unloaded");
  return 0;
}

int loop_mymodule_lua_module(void) {
  if (!g_host.L || !g_host.script_loaded) {
    return -1;
  }

  // 更新重力传感器快照
  lua_hardware_update_gravity();

  // 调用 loop 函数
  lua_getglobal(g_host.L, "loop");
  if (lua_isfunction(g_host.L, -1)) {
    if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(g_host.L, -1);
      Serial.printf("[mymodule_lua] ERROR in loop: %s\n", error);
      lua_pop(g_host.L, 1);
      return -2;
    }
  } else {
    lua_pop(g_host.L, 1);
  }

  return 0;
}

String mymodule_lua_module_runtime_status_json(void) {
  return "{}";
}

String mymodule_lua_module_configs_json(void) {
  return "[]";
}
```

### 步骤 3：注册模块

编辑 `src/module_registry.cpp`：

```cpp
// 1. 添加头文件
#include "mymodule_lua.h"

// 2. 在 k_modules 数组中添加模块描述
static const module_descriptor_t k_modules[] = {
  // ... 其他模块 ...
  {"mymodule", "MyModule", "1.0.0", "YourName", "My custom effect", "lua", "lua-5.4.7", 
   nullptr, setup_mymodule_lua_module, unload_mymodule_lua_module, loop_mymodule_lua_module, 
   nullptr, 0, true},
};
```

### 步骤 4：编译和上传

```bash
# 编译
pio run

# 上传到设备
pio run --target upload

# 查看串口输出
pio device monitor
```

---

## 调色盘系统

### 预定义调色盘示例

#### 彩虹调色盘
```lua
local rainbow_palette = {
  {0, 255, 0, 0},       -- 红
  {42, 255, 165, 0},    -- 橙
  {85, 255, 255, 0},    -- 黄
  {127, 0, 255, 0},     -- 绿
  {170, 0, 255, 255},   -- 青
  {212, 0, 0, 255},     -- 蓝
  {255, 255, 0, 255}    -- 品红
}
```

#### 火焰调色盘
```lua
local fire_palette = {
  {0, 0, 0, 0},         -- 黑
  {64, 128, 0, 0},      -- 暗红
  {128, 255, 0, 0},     -- 红
  {192, 255, 128, 0},   -- 橙
  {255, 255, 255, 0}    -- 黄
}
```

#### 海洋调色盘
```lua
local ocean_palette = {
  {0, 0, 0, 64},        -- 深蓝
  {64, 0, 64, 128},     -- 蓝
  {128, 0, 128, 255},   -- 青
  {192, 0, 255, 200},   -- 浅青
  {255, 128, 255, 255}  -- 白青
}
```

### 使用调色盘

```lua
-- 创建渐变效果
for x = 0, WIDTH - 1 do
  local color_index = x * 255 / WIDTH
  local r, g, b = led.palette(rainbow_palette, color_index, 180)
  for y = 0, HEIGHT - 1 do
    led.set(x, y, r, g, b)
  end
end
```

---

## 最佳实践

### 1. 性能优化

**避免在循环中创建表：**
```lua
-- ❌ 不好
function loop()
  local temp = {}  -- 每帧创建新表
  -- ...
end

-- ✅ 好
local temp = {}  -- 在外部创建一次
function loop()
  -- 使用 temp
end
```

**使用局部变量：**
```lua
-- ✅ 局部变量访问更快
local w = WIDTH
local h = HEIGHT
for x = 0, w - 1 do
  for y = 0, h - 1 do
    -- ...
  end
end
```

### 2. 时间控制

**使用时间戳控制更新频率：**
```lua
local last_update = 0
local update_interval = 50  -- 50ms

function loop()
  local now = time.millis()
  
  if now - last_update >= update_interval then
    last_update = now
    -- 执行更新逻辑
  end
end
```

### 3. 状态管理

**使用模块级变量保存状态：**
```lua
-- 状态变量
local phase = 0
local direction = 0
local color_offset = 0

function loop()
  -- 更新状态
  phase = (phase + 1) % 256
  
  -- 使用状态
  -- ...
end
```

### 4. 错误处理

**检查传感器数据有效性：**
```lua
local gx, gy, gz, valid = gravity.get()
if not valid then
  gx, gy, gz = 0, 0, 0  -- 使用默认值
end
```

### 5. 内存管理

**避免频繁分配大表：**
```lua
-- ✅ 预分配数组
local buffer = {}
for i = 0, WIDTH - 1 do
  buffer[i] = 0
end

function loop()
  -- 重用 buffer
end
```

---

## 调试技巧

### 1. 串口输出

Lua 脚本中无法直接使用 `print()`，但可以在 C++ 代码中添加调试输出：

```cpp
// 在 loop_mymodule_lua_module() 中添加
Serial.printf("[mymodule_lua] Debug: value=%d\n", some_value);
```

### 2. 错误信息

Lua 错误会自动输出到串口：

```
[mymodule_lua] ERROR in loop: [string "..."]:42: attempt to index a nil value
```

### 3. LED 调试

使用特定颜色指示状态：

```lua
-- 红色表示错误
led.set(0, 0, 255, 0, 0)

-- 绿色表示正常
led.set(0, 0, 0, 255, 0)

-- 蓝色表示调试点
led.set(0, 0, 0, 0, 255)
```

### 4. 分步测试

逐步添加功能，每次只测试一个特性：

```lua
function loop()
  led.clear()
  
  -- 步骤 1: 测试基本显示
  -- led.set(0, 0, 255, 0, 0)
  
  -- 步骤 2: 测试循环
  -- for x = 0, WIDTH - 1 do
  --   led.set(x, 0, 255, 0, 0)
  -- end
  
  -- 步骤 3: 测试动画
  -- ...
  
  led.show()
end
```

---

## 示例代码

### 示例 1：呼吸灯效果

```lua
local brightness = 0
local direction = 1

function setup()
  led.clear()
  led.show()
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
  
  -- 全屏显示
  for x = 0, WIDTH - 1 do
    for y = 0, HEIGHT - 1 do
      led.set(x, y, brightness, brightness, brightness)
    end
  end
  
  led.show()
end

function unload()
  led.clear()
  led.show()
end
```

### 示例 2：音频可视化柱状图

```lua
local bar_heights = {}

-- 初始化
for i = 0, WIDTH - 1 do
  bar_heights[i] = 0
end

function setup()
  led.clear()
  led.show()
end

function loop()
  led.clear()
  
  -- 处理 FFT 数据
  for x = 0, WIDTH - 1 do
    -- 读取对应频段
    local bin_start = x * 30
    local sum = 0
    for b = bin_start, bin_start + 29 do
      sum = sum + fft.get(b)
    end
    local avg = sum / 30
    
    -- 缩放到屏幕高度
    local height = math.floor(avg * HEIGHT / 100)
    height = math.min(height, HEIGHT - 1)
    bar_heights[x] = height
    
    -- 绘制柱状图
    for y = 0, height do
      local hue = y * 255 / HEIGHT
      local r, g, b = led.hsv(hue, 255, 180)
      led.set(x, y, r, g, b)
    end
  end
  
  led.show()
end

function unload()
  led.clear()
  led.show()
end
```

### 示例 3：重力控制的小球

```lua
local ball_x = WIDTH / 2
local ball_y = HEIGHT / 2
local vel_x = 0
local vel_y = 0

function setup()
  led.clear()
  led.show()
end

function loop()
  led.clear()
  
  -- 读取重力
  local gx, gy, gz, valid = gravity.get()
  if valid then
    -- 重力加速度
    vel_x = vel_x + gx * 0.5
    vel_y = vel_y + gy * 0.5
    
    -- 阻尼
    vel_x = vel_x * 0.9
    vel_y = vel_y * 0.9
    
    -- 更新位置
    ball_x = ball_x + vel_x
    ball_y = ball_y + vel_y
    
    -- 边界碰撞
    if ball_x < 0 then
      ball_x = 0
      vel_x = -vel_x * 0.8
    elseif ball_x >= WIDTH then
      ball_x = WIDTH - 1
      vel_x = -vel_x * 0.8
    end
    
    if ball_y < 0 then
      ball_y = 0
      vel_y = -vel_y * 0.8
    elseif ball_y >= HEIGHT then
      ball_y = HEIGHT - 1
      vel_y = -vel_y * 0.8
    end
  end
  
  -- 绘制小球
  local x = math.floor(ball_x)
  local y = math.floor(ball_y)
  led.set(x, y, 255, 255, 255)
  
  led.show()
end

function unload()
  led.clear()
  led.show()
end
```

### 示例 4：矩阵雨效果

```lua
local drops = {}
local last_spawn = 0

-- 初始化雨滴
for x = 0, WIDTH - 1 do
  drops[x] = {y = -1, speed = 0}
end

function setup()
  led.clear()
  led.show()
end

function loop()
  local now = time.millis()
  led.clear()
  
  -- 生成新雨滴
  if now - last_spawn > 100 then
    last_spawn = now
    local x = math.random(0, WIDTH - 1)
    if drops[x].y < 0 then
      drops[x].y = 0
      drops[x].speed = math.random(1, 3)
    end
  end
  
  -- 更新和绘制雨滴
  for x = 0, WIDTH - 1 do
    local drop = drops[x]
    if drop.y >= 0 then
      -- 绘制拖尾
      for i = 0, 3 do
        local y = drop.y - i
        if y >= 0 and y < HEIGHT then
          local brightness = 255 - i * 60
          led.set(x, y, 0, brightness, 0)
        end
      end
      
      -- 移动雨滴
      drop.y = drop.y + drop.speed
      if drop.y >= HEIGHT + 3 then
        drop.y = -1
      end
    end
  end
  
  led.show()
end

function unload()
  led.clear()
  led.show()
end
```

---

## 常见问题

### Q: 为什么我的动画很卡顿？
A: 检查以下几点：
1. 减少嵌套循环的复杂度
2. 使用局部变量缓存常用值
3. 避免在循环中创建新表
4. 控制更新频率（使用 `time.millis()` 限制帧率）

### Q: 如何切换不同的效果？
A: 使用 `sys.page_index()` 获取当前子页面索引：

```lua
function loop()
  local style = sys.page_index() % 4
  
  if style == 0 then
    -- 效果 1
  elseif style == 1 then
    -- 效果 2
  elseif style == 2 then
    -- 效果 3
  elseif style == 3 then
    -- 效果 4
  end
end
```

### Q: 如何保存配置？
A: 配置会自动保存到 NVS（非易失性存储）中，使用 `kDefaultConfigKey` 作为键名。

### Q: Lua 5.1 有哪些限制？
A: 主要限制：
- 不支持 `//` 整数除法运算符（使用 `math.floor(a / b)`）
- 不支持位运算符（使用 `bit` 库或数学运算）
- 表索引从 1 开始（但 LED 坐标从 0 开始）

---

## 进阶主题

### 自定义 C++ API

如果需要添加新的硬件功能，可以在 `lua_hardware_api.cpp` 中扩展：

```cpp
// 添加新的 API 函数
static int lua_custom_function(lua_State* L) {
  int param = (int)luaL_checknumber(L, 1);
  
  // 你的逻辑
  int result = param * 2;
  
  lua_pushnumber(L, result);
  return 1;
}

// 注册到 Lua
void register_lua_hardware_apis(lua_State* L) {
  // ... 现有代码 ...
  
  // 注册自定义函数
  lua_newtable(L);
  lua_pushcfunction(L, lua_custom_function);
  lua_setfield(L, -2, "my_function");
  lua_setglobal(L, "custom");
}
```

在 Lua 中使用：

```lua
local result = custom.my_function(42)
```

---

## 资源链接

- **Lua 5.1 参考手册**: https://www.lua.org/manual/5.1/
- **FastLED 文档**: https://fastled.io/
- **ESP32 文档**: https://docs.espressif.com/

---

## 贡献

欢迎提交你的模块代码！请遵循以下规范：

1. 代码风格一致
2. 添加详细注释
3. 提供使用示例
4. 测试所有功能

---

**祝你开发愉快！** 🎨✨
