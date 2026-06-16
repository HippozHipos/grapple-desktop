#pragma once

#include <grapple/render/RenderFrame.hpp>

#include <QString>

namespace grapple::ui {

[[nodiscard]] QString evaluatedClipEffectText(const render::RenderedMediaFrame& mediaFrame);
[[nodiscard]] QString prefixedEvaluatedClipEffectText(const render::RenderedMediaFrame& mediaFrame);

} // namespace grapple::ui
