#define WLR_USE_UNSTABLE

#include <any>
#include <array>
#include <cmath>
#include <climits>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "globals.hpp"
#include "CornerPassElement.hpp"

using namespace Render;
using namespace Render::GL;

static void damageMonitorCorners(PHLMONITOR monitor) {
    // Damage the largest of the previous / current radius to also clear stale
    // corners when the radius shrinks or gets disabled.
    const double RADIUS = std::max(static_cast<double>(g_config->radius), static_cast<double>(g_config->prevRadius));
    if (RADIUS < 1)
        return;

    const auto BOUNDS = monitor->m_size;
    for (const auto& offset :
         std::array<Vector2D, 4>{Vector2D{0., 0.}, Vector2D{BOUNDS.x - RADIUS, 0.}, Vector2D{0., BOUNDS.y - RADIUS}, Vector2D{BOUNDS.x - RADIUS, BOUNDS.y - RADIUS}}) {
        g_pHyprRenderer->damageBox(CBox(monitor->m_position.x + offset.x, monitor->m_position.y + offset.y, RADIUS, RADIUS));
    }
}

static void damageCorners(PHLMONITOR monitor) {
    // Damage the corner regions (logical/global coords) each frame so they are
    // always re-rendered and our paint never gets lost.
    const double RADIUS = g_config->radius;
    if (RADIUS < 1)
        return;

    const auto BOUNDS = monitor->m_size;
    for (const auto& offset :
         std::array<Vector2D, 4>{Vector2D{0., 0.}, Vector2D{BOUNDS.x - RADIUS, 0.}, Vector2D{0., BOUNDS.y - RADIUS}, Vector2D{BOUNDS.x - RADIUS, BOUNDS.y - RADIUS}}) {
        g_pHyprRenderer->damageBox(CBox(monitor->m_position.x + offset.x, monitor->m_position.y + offset.y, RADIUS, RADIUS));
    }
}

static void onRenderStage(const Event::SRenderStageEvent& e) {
    // Adding the pass element in the last stage makes it render last in the
    // pass, i.e. on top of everything else in the frame.
    if (e.stage != RENDER_LAST_MOMENT)
        return;

    if (!e.monitor || !e.context.has_value())
        return;

    g_pHyprRenderer->addPassElement(e.context->get(), makeUnique<CCornerPassElement>(CCornerPassElement::SCornerData{e.monitor}));

    // Keep the corner regions dirty so the element is always re-rendered.
    damageCorners(e.monitor);
}

static void onConfigReloaded() {
    // Refresh local radius, damaging a superset of what we have ever painted.
    g_config->prevRadius = g_config->radius;
    g_config->radius     = std::clamp(sc<int>(g_config->size->value()), 0, INT_MAX);
    g_config->dirty = true; // texture needs a rebuild if color changed
    for (auto& m : State::monitorState()->monitors())
        damageMonitorCorners(m);
}

// ─────────────────────────────────────────────────────────────────────────────
// Plugin entry
// ─────────────────────────────────────────────────────────────────────────────

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[screen-corners] Failed to init: version mismatch (headers vs running Hyprland)", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[screen-corners] Version mismatch");
    }

    g_config        = makeUnique<SConfig>();
    g_config->size  = makeShared<Config::Values::CIntValue>("plugin:screencorners:size", "Screen corner radius in logical px", 12);
    g_config->color = makeShared<Config::Values::CColorValue>("plugin:screencorners:color", "Color of the corners (fully opaque recommended)", 0xFF000000);

    HyprlandAPI::addConfigValueV2(PHANDLE, g_config->size);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config->color);

    static auto P       = Event::bus()->m_events.render.stage.listen(::onRenderStage);
    static auto PCONFIG = Event::bus()->m_events.config.reloaded.listen(::onConfigReloaded);

    ::onConfigReloaded();

    // One full redraw so no stale pixels (e.g. from previous plugin versions) linger in the corners.
    for (auto& m : State::monitorState()->monitors())
        g_pHyprRenderer->damageBox(CBox{m->m_position.x, m->m_position.y, m->m_size.x, m->m_size.y});

    return {"screen-corners", "A tiny plugin that paints rounded corners onto every monitor.", "David", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Force a redraw of the corner areas so the black corners don't linger after unload.
    for (auto& m : State::monitorState()->monitors())
        damageMonitorCorners(m);
}
