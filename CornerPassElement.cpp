#include "CornerPassElement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include "globals.hpp"

using namespace Render;
using namespace Render::GL;

CCornerPassElement::CCornerPassElement(const CCornerPassElement::SCornerData& data_) : data(data_) {
    ;
}

// The corner sliver (pixels outside a quarter-circle in each corner of the
// monitor) is rasterized on the CPU with a smooth per-pixel feather, uploaded
// once as a tiny ARGB texture per side (mirrored), and drawn through the
// canonical CTexPassElement path with an explicit full-frame damage, exactly
// like the wallpaper/background rendering does.
void CCornerPassElement::drawDirect(Render::CRenderContext& ctx, PHLMONITOR PMONITOR) {
    const auto RADIUS = std::lround(static_cast<double>(g_config->radius) * PMONITOR->m_scale);
    if (RADIUS < 1)
        return;

    const auto BOUNDS = PMONITOR->m_pixelSize;
    const auto MIN    = static_cast<double>(std::min(BOUNDS.x, BOUNDS.y));
    if (2.0 * RADIUS > MIN) // corners would overlap
        return;

    const CHyprColor COLOR = CHyprColor(static_cast<uint64_t>(g_config->color->value()));
    if (COLOR.a == 0)
        return;

    // (Re)build the corner mask textures when the plugin initialises or the
    // config changes. One texture per corner: the quarter-circle sliver has to
    // look mirrored per side.
    static std::array<SP<Render::ITexture>, 4> MASKS = {nullptr, nullptr, nullptr, nullptr};
    static int                                 maskRadius = -1;
    if (std::any_of(MASKS.begin(), MASKS.end(), [](const auto& t) { return !t; }) || maskRadius != RADIUS || g_config->dirty.exchange(false)) {
        maskRadius = RADIUS;

        const int      STRIDE = RADIUS * 4;
        const double   Rc     = static_cast<double>(RADIUS);
        const uint32_t RB     = sc<uint32_t>(std::round(std::clamp(COLOR.r, 0.0, 1.0) * 255.0));
        const uint32_t GB     = sc<uint32_t>(std::round(std::clamp(COLOR.g, 0.0, 1.0) * 255.0));
        const uint32_t BB     = sc<uint32_t>(std::round(std::clamp(COLOR.b, 0.0, 1.0) * 255.0));
        const double   CA     = std::clamp(COLOR.a, 0.0, 1.0);

        // corner variants: (mirror x, mirror y) = TL, TR, BL, BR
        for (int v = 0; v < 4; ++v) {
            const bool MX = v & 1, MY = v & 2;
            std::vector<uint8_t> pixels(sc<size_t>(STRIDE) * RADIUS, 0);
            for (int y = 0; y < RADIUS; ++y) {
                for (int x = 0; x < RADIUS; ++x) {
                    // pixel center relative to the arc center (Rc, Rc), in the
                    // corner frame (mirrored for the right/bottom corners)
                    const double lx = (MX ? (RADIUS - 1 - x) + 0.5 : x + 0.5) - Rc;
                    const double ly = (MY ? (RADIUS - 1 - y) + 0.5 : y + 0.5) - Rc;
                    // coverage with a 1px smooth feather: 0 inside the round
                    // area, 1 solidly outside it (the visible corner sliver)
                    const double cov = std::clamp((std::hypot(lx, ly) - Rc) + 0.5, 0.0, 1.0);
                    if (cov <= 0.0)
                        continue;

                    const uint32_t A = sc<uint32_t>(std::round(cov * CA * 255.0));
                    // premultiplied ARGB8888, little endian = B,G,R,A bytes
                    const uint32_t px = (A << 24) | (sc<uint32_t>(std::round(BB * (A / 255.0))) << 0) | (sc<uint32_t>(std::round(GB * (A / 255.0))) << 8) |
                        (sc<uint32_t>(std::round(RB * (A / 255.0))) << 16);
                    std::memcpy(&pixels[sc<size_t>(y) * STRIDE + sc<size_t>(x) * 4], &px, 4);
                }
            }

            MASKS[v] = g_pHyprRenderer->createTexture(0x34325241 /* DRM_FORMAT_ARGB8888 */, pixels.data(), STRIDE, Vector2D(static_cast<double>(RADIUS), static_cast<double>(RADIUS)), true, false);
        }
    }

    // The pass element runs with an empty ctx damage; use an explicit
    // full-monitor region for our draws so they aren't scissored away.
    static CRegion fullMonitorDamage;
    fullMonitorDamage.clear();
    fullMonitorDamage.add(CBox{0, 0, BOUNDS.x, BOUNDS.y});

    const double RD = static_cast<double>(RADIUS);
    for (int i = 0; i < 4; ++i) {
        double px = 0.0, py = 0.0;
        switch (i) {
            case 0: px = 0.0; py = 0.0; break;                     // top-left
            case 1: px = BOUNDS.x - RD; py = 0.0; break;           // top-right
            case 2: px = 0.0; py = BOUNDS.y - RD; break;           // bottom-left
            case 3: px = BOUNDS.x - RD; py = BOUNDS.y - RD; break; // bottom-right
        }

        CBox box = CBox(px, py, RD, RD);
        box.round();

        CTexPassElement::SRenderData texData = {.tex = MASKS[i], .box = box, .a = 1.F};
        texData.damage                       = CRegion{box};
        texData.useProvidedDamage            = true;
        g_pHyprRenderer->draw(ctx, texData, fullMonitorDamage);
    }
}

// Pass element entry: delegates to drawDirect with the stored monitor.
std::vector<UP<IPassElement>> CCornerPassElement::draw(Render::CRenderContext& ctx) {
    if (const auto PMONITOR = data.monitor.lock())
        drawDirect(ctx, PMONITOR);
    return {};
}

bool CCornerPassElement::needsLiveBlur(Render::CRenderContext&) {
    return false;
}

bool CCornerPassElement::needsPrecomputeBlur(Render::CRenderContext&) {
    return false;
}

std::optional<CBox> CCornerPassElement::boundingBox(Render::CRenderContext& ctx) {
    // Full monitor box: never culled by occlusion logic.
    const auto PMONITOR = data.monitor.lock();
    if (!PMONITOR)
        return std::nullopt;

    return CBox{0, 0, PMONITOR->m_size.x, PMONITOR->m_size.y};
}

bool CCornerPassElement::disableSimplification(Render::CRenderContext& ctx) {
    return true;
}
