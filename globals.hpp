#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>

#include <memory>
#include <atomic>

inline HANDLE PHANDLE = nullptr;

struct SConfig {
    int                             radius = 12;
    int                             prevRadius = 0; // radius before the last config change, to damage everything we may have painted
    std::atomic<bool>                dirty{false};
    SP<Config::Values::CIntValue>   size;    SP<Config::Values::CColorValue> color;
};

inline UP<SConfig> g_config = nullptr;
