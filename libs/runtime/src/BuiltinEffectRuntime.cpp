#include <grapple/runtime/BuiltinEffectRuntime.hpp>

#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/runtime/RuntimeOutputNames.hpp>

#include <optional>
#include <string>
#include <variant>

namespace grapple::runtime {

namespace {

std::optional<double> numericParam(const RuntimeParamSet& params, const std::string& name) {
  for (const RuntimeParam& param : params) {
    if (param.name == name) {
      const auto* value = std::get_if<double>(&param.value);
      if (value != nullptr) {
        return *value;
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

RuntimeDiagnostic makeParamDiagnostic(
  const EffectPrepareRequest& request,
  const std::string& paramName
) {
  return RuntimeDiagnostic{
    "runtime.builtin_camera_transform_param_invalid",
    DiagnosticSeverity::Error,
    DiagnosticLocation{
      request.projectId,
      request.revision,
      request.node.sourceNodeId
    },
    "Builtin camera_transform effect requires numeric parameter " + paramName + "."
  };
}

} // namespace

bool BuiltinEffectRuntime::supports(const projection::RenderEffectNode& node) const {
  return node.payload.implementation.kind == timeline::EffectImplementationKind::Builtin &&
         node.payload.implementation.entrypoint == builtin_effect::CameraTransformEntrypoint;
}

foundation::Result<EffectPrepareResult> BuiltinEffectRuntime::prepare(const EffectPrepareRequest& request) {
  std::vector<RuntimeDiagnostic> diagnostics;
  const RuntimeParamSet params = runtimeParamsFromEffectNode(request.node);
  const std::optional<double> positionX = numericParam(params, builtin_effect::PositionXParam);
  const std::optional<double> positionY = numericParam(params, builtin_effect::PositionYParam);
  const std::optional<double> zoom = numericParam(params, builtin_effect::ZoomParam);

  if (!positionX.has_value()) {
    diagnostics.push_back(makeParamDiagnostic(request, builtin_effect::PositionXParam));
  }
  if (!positionY.has_value()) {
    diagnostics.push_back(makeParamDiagnostic(request, builtin_effect::PositionYParam));
  }
  if (!zoom.has_value()) {
    diagnostics.push_back(makeParamDiagnostic(request, builtin_effect::ZoomParam));
  }

  RuntimeValueMap preparedValues;
  if (positionX.has_value() && positionY.has_value() && zoom.has_value()) {
    preparedValues.push_back(RuntimeNamedValue{builtin_effect::PositionXParam, RuntimeValue{*positionX}});
    preparedValues.push_back(RuntimeNamedValue{builtin_effect::PositionYParam, RuntimeValue{*positionY}});
    preparedValues.push_back(RuntimeNamedValue{builtin_effect::ZoomParam, RuntimeValue{*zoom}});
  }

  return EffectPrepareResult{
    PreparedEffectNode{
      request.graph.id,
      request.graph.targetNodeId,
      request.node.sourceNodeId,
      request.node.payload.activeRange,
      nullptr,
      params,
      std::move(preparedValues)
    },
    diagnostics
  };
}

foundation::Result<EffectProcessResult> BuiltinEffectRuntime::process(const EffectProcessRequest& request) {
  std::optional<double> positionX;
  std::optional<double> positionY;
  std::optional<double> zoom;

  for (const RuntimeNamedValue& value : request.params) {
    const auto* numeric = std::get_if<double>(&value.value);
    if (numeric == nullptr) {
      continue;
    }
    if (value.name == builtin_effect::PositionXParam) {
      positionX = *numeric;
    } else if (value.name == builtin_effect::PositionYParam) {
      positionY = *numeric;
    } else if (value.name == builtin_effect::ZoomParam) {
      zoom = *numeric;
    }
  }

  RuntimeValueMap outputValues;
  if (positionX.has_value() && positionY.has_value() && zoom.has_value()) {
    outputValues.push_back(RuntimeNamedValue{
      output_name::CameraTransform,
      RuntimeValue{
        foundation::Transform2D{
          foundation::Vec2{*positionX, *positionY},
          foundation::Vec2{*zoom, *zoom},
          0.0,
          1.0
        }
      }
    });
  }

  return EffectProcessResult{
    RuntimeEffectOutput{
      request.prepared.effectGraphId,
      request.prepared.targetNodeId,
      request.prepared.sourceNodeId,
      std::move(outputValues)
    },
    {}
  };
}

} // namespace grapple::runtime
