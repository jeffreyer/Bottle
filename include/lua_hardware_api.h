#pragma once

#include <lua.hpp>

// 注册所有硬件相关的 Lua API
// 包括: led, spectrum, gravity, config, time
void register_lua_hardware_apis(lua_State* L);

// 更新重力传感器快照（在调用 Lua 脚本前调用）
void lua_hardware_update_gravity();
