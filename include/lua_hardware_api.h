#pragma once

#include <lua.hpp>

// 注册所有硬件相关的 Lua API
// 包括: led, spectrum, gravity, config, time
void register_lua_hardware_apis(lua_State* L);

// 注入 CONFIG 全局表（从 NVS 读取配置）
void inject_lua_config_table(lua_State* L, const char* module_id);

// 更新重力传感器快照（在调用 Lua 脚本前调用）
void lua_hardware_update_gravity();

// 启动已声明的硬件资源
void lua_hardware_start_resources();

// 停止所有硬件资源
void lua_hardware_stop_resources();
