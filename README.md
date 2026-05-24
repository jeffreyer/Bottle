# Bottle - 可编程 LED 矩阵设备

Bottle 是一个基于 ESP32-S3 的可编程 LED 矩阵设备，支持通过 Lua 脚本创建动态动画效果。它集成了重力感应、音频采集、蓝牙配置等功能，适合创建互动式视觉艺术作品。

## 主要特性

### 硬件能力
- **LED 矩阵**: 17×8 全彩 LED 阵列，支持动态动画
- **重力感应**: LIS3DH 三轴加速度计，支持姿态检测和重力交互
- **音频采集**: 支持 I2S/PDM 麦克风，实时 FFT 频谱分析（17 频段）
- **音频录制**: 支持录制原始 PCM 音频到 U盘
- **触摸控制**: 电容触摸按键，支持短按、长按、超长按
- **蓝牙功能**: 
  - BLE 配置服务（通过手机 App 配置设备）
  - BLE HID 鼠标模式（将设备用作蓝牙鼠标）
- **USB 功能**: 
  - **USB 复合设备**: 同时支持串口（CDC）和 U盘（MSC）
  - **串口调试**: 始终可用
- **电源管理**: 电池电量监测、自动休眠、低功耗模式

### 软件特性
- **Lua 脚本引擎**: 基于 Lua 5.4.7，支持热加载动画模块
- **模块化架构**: 内置多种动画效果（文字滚动、频谱显示、重力粒子、沙漏、蜡烛等）
- **云端配置**: 支持通过蓝牙从云端下载和配置模块
- **安全机制**: 栈溢出保护、数组边界检查、指令限制
- **持久化存储**: NVS 配置存储、Flash 文件系统

## 硬件设计

本项目包含完整的硬件设计文件（Altium Designer 格式）：

- **PCB 设计**: [Hardware](Hardware/) 目录
  - 硬件组装说明: [README.md](Hardware/README.md)
  - 主板原理图: [main.schdoc](Hardware/main.schdoc)
  - LED 阵列原理图: [led_array.schdoc](Hardware/led_array.schdoc)
  - PCB 布局: [bottle.PcbDoc](Hardware/bottle.PcbDoc)
  - Gerber 文件: [Gerber.rar](Hardware/Gerber.rar)

**核心器件**:
- MCU: ESP32-S3 (N4R2 配置 - 4MB Flash, 2MB PSRAM)
- 加速度计: LIS3DH
- LED 驱动: WS2812B 兼容
- 麦克风: I2S/PDM 数字麦克风（板子支持MSM261D3526Z1CM，MSM261S3526Z0CM，软件可通过命令行切换）

## 快速开始

### 环境准备

1. **安装开发工具**
   - [Visual Studio Code](https://code.visualstudio.com/)
   - [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) 扩展

2. **克隆仓库**
   ```bash
   git clone <repo-url>
   cd Bottle
   ```

### 编译和上传

```bash
# 编译固件
pio run

# 上传到 ESP32-S3
pio run -t upload

# 打开串口监视器
pio device monitor
```

### 基本操作

**触摸按键控制**:
- **短按**: 切换子页面/模式
- **长按 (大于1秒)**: 正常模式时：在切换动画模块/蓝牙配置模式/关机功能循环，松开即执行显示的功能；蓝牙模式时：在退出蓝牙模式/关机功能循环
- **超长按 (>15秒)**: 强制关机

## Lua 脚本开发

Bottle 支持通过 Lua 脚本创建自定义动画效果。详细的 API 文档和示例请参考：

📖 **[Lua 脚本开发指南](docs/LUA_SCRIPT_GUIDE.md)**

### 快速示例

```lua
-- @name: 彩虹波浪
-- @version: 1.0.0
-- @author: Your Name
-- @description: Rainbow wave animation
-- @id: rainbow

function setup()
  led.clear()
  led.show()
end

function loop()
  local t = time.millis() / 50
  
  for x = 0, 16 do
    for y = 0, 7 do
      local hue = (x * 15 + y * 10 + t) % 255
      local r, g, b = led.hsv(hue, 255, 255)
      led.set(x, y, r, g, b)
    end
  end
  
  led.show()
  time.delay(30)
end

function unload()
  led.clear()
  led.show()
end
```

### 可用的硬件 API

- **LED 控制**: `led.set()`, `led.clear()`, `led.show()`, `led.text()`, `led.hsv()`
- **时间**: `time.millis()`, `time.delay()`
- **重力感应**: `gravity.get()` - 返回 X/Y/Z 轴加速度
- **音频频谱**: `audio.init()`, `audio.getSpectrum()`, `audio.close()`

## 内置动画模块

- **文字显示** (`text`) - 支持滚动文字、自定义颜色
- **频谱显示** (`rhythm`) - 音频频谱可视化
- **重力粒子** - 重力感应控制的粒子效果
- **沙漏** (`sandglass`) - 重力感应沙漏模拟
- **蜡烛** (`candle`) - 蜡烛火焰动画
- **水波模拟** (`water_sim`) - 流体动力学模拟
- **RGB 彩虹** (`rgb`) - 彩虹渐变动画
- 以上部分模块需通过小程序**幻彩抽屉**下载

## 蓝牙配置

设备支持通过 BLE 进行配置：

1. 长按触摸键 2-3 秒进入 BLE 配置模式
2. 使用微信小程序**幻彩抽屉**连接设备（设备名: "BottleLED"）
3. 通过 小程序 配置模块参数、下载新模块、调整系统设置
4. 再次长按退出配置模式

**BLE 服务 UUID**: 自定义配置服务（参见 [ble_config.cpp](src/ble_config.cpp)）

## 音频功能

### 实时频谱分析
- 17 频段 FFT 分析（0-8kHz）
- 支持 I2S 和 PDM 麦克风
- 可用于音乐可视化、声控动画

### 音频录制
- 录制格式: 16-bit PCM, 16kHz 采样率
- 存储位置: `/extflash/rec_*.pcm`
- 通过 USB U盘导出录音文件

## 电源管理

- **电池监测**: 实时监测电池电压
- **自动休眠**: 可配置的空闲超时（默认 60 秒）
- **低功耗模式**: 休眠时关闭 LED、传感器、蓝牙
- **唤醒方式**: 触摸按键唤醒

## 开发和调试

### USB 复合设备

设备使用 **USB 复合设备模式**，同时提供串口和 U盘功能：

**串口（CDC）**:
- 始终可用
- 波特率: 115200
- 用于调试输出和命令行交互

**U盘（MSC）**:
- 可通过串口命令 `usb=1` 启用（可选）
- 启用后设备会显示为 USB 大容量存储设备
- 可访问 `/extflash` 分区
- 用于上传 Lua 脚本、导出录音文件等
- 串口和 U盘可以同时使用

**使用流程**:
```bash
# 1. 连接 USB 线到电脑
# 2. 打开串口终端（115200 波特率）
pio device monitor

# 3. （可选）启用 U盘模式
usb=1

# 4. 电脑会识别出 U盘，可以读写文件
# 5. 串口功能始终可用，可以同时调试
```

**注意事项**:
- U盘模式启用时，设备内部无法访问 `/extflash` 文件系统
- 禁用 U盘模式后（`usb=0` 或安全弹出），文件系统会自动重新挂载
- 串口和 U盘可以同时工作，互不干扰

### 串口调试

设备通过 USB 串口输出调试信息（115200 波特率）：

```bash
# 使用 PlatformIO 监视器
pio device monitor

# 或使用其他串口工具
# Windows: PuTTY, TeraTerm
# Linux/Mac: screen, minicom
```

### 添加新模块

1. 在 [src/](src/) 目录创建模块源文件
2. 实现 `setup()`, `loop()`, `unload()` 函数
3. 在 [module_registry.cpp](src/module_registry.cpp) 注册模块
4. 重新编译上传

或者使用 Lua 脚本（推荐）：

1. 编写 Lua 脚本（参考 [Lua 开发指南](docs/LUA_SCRIPT_GUIDE.md)）
2. 通过蓝牙上传到设备
3. 无需重新编译固件

## 依赖库

- **[FastLED](https://github.com/FastLED/FastLED)** (v3.10.3) - LED 控制库
- **[arduinoFFT](https://github.com/kosme/arduinoFFT)** (v2.0.4) - FFT 实现
- **[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)** (v2.3.9) - 蓝牙协议栈
- **[HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse)** - BLE HID 鼠标库
- **Lua 5.4.7** - 脚本引擎

## 贡献指南

欢迎贡献代码、报告问题或提出建议！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交 Pull Request

**贡献内容**:
- 新的 Lua 动画模块
- 硬件改进和优化
- Bug 修复
- 文档改进
- 性能优化

## 许可证

- **固件代码**: [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)
- **硬件设计**: [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)

**商业使用需获得作者书面许可。**

## 致谢
- **好心却黑猫35** - [我制作了一瓶电子海洋](https://www.bilibili.com/video/BV1f2PezXEso/)
- **FastLED** - 强大的 LED 控制库
- **arduinoFFT** - 高效的 FFT 实现
- **Lua** - 优雅的嵌入式脚本语言
- **ESP32 Arduino Core** - ESP32 开发框架
- **NimBLE** - 轻量级蓝牙协议栈

## 联系方式

- **问题反馈**: 请在 GitHub Issues 提交

---

**版本**: 2.2  
**硬件版本**: Bottle v2.2 PCBA
