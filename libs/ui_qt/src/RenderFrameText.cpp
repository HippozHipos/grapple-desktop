#include <grapple/ui_qt/RenderFrameText.hpp>

#include <QStringList>

namespace grapple::ui {

QString evaluatedClipEffectText(const render::RenderedMediaFrame& mediaFrame) {
  QStringList effects;
  if (mediaFrame.tintColor.has_value() && mediaFrame.tintAmount != 0.0) {
    effects << QString{"Tint %1%"}.arg(mediaFrame.tintAmount * 100.0, 0, 'f', 0);
  }
  if (mediaFrame.exposure != 0.0) {
    effects << QString{"Exposure %1 EV"}.arg(mediaFrame.exposure, 0, 'f', 2);
  }
  if (effects.isEmpty()) {
    return "No evaluated clip effects";
  }
  return effects.join(", ");
}

QString prefixedEvaluatedClipEffectText(const render::RenderedMediaFrame& mediaFrame) {
  const QString effects = evaluatedClipEffectText(mediaFrame);
  if (effects == "No evaluated clip effects") {
    return effects;
  }
  return QString{"Effects: %1"}.arg(effects);
}

} // namespace grapple::ui
