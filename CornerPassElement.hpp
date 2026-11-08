#pragma once
#include <hyprland/src/render/pass/PassElement.hpp>

#include "globals.hpp"

class CCornerPassElement : public IPassElement {
  public:
    struct SCornerData {
        PHLMONITORREF monitor;
    };

    CCornerPassElement(const SCornerData& data_);
    virtual ~CCornerPassElement() = default;

    static void drawDirect(Render::CRenderContext& ctx, PHLMONITOR monitor);

    virtual std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    virtual bool                          needsLiveBlur(Render::CRenderContext&) override;
    virtual bool                          needsPrecomputeBlur(Render::CRenderContext&) override;
    virtual std::optional<CBox>           boundingBox(Render::CRenderContext& ctx) override;
    virtual bool                          disableSimplification(Render::CRenderContext& ctx) override;

    virtual const char*                   passName() override {
        return "CCornerPassElement";
    }

    virtual ePassElementType type() override {
        return EK_CUSTOM;
    }

  private:
    SCornerData data;
};
