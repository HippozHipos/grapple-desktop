#include <grapple/runtime/BuiltinEffectRuntime.hpp>

#include <grapple/runtime/RuntimeOutputNames.hpp>

#include <optional>
#include <string>
#include <variant>

namespace grapple::runtime {

namespace {

inline constexpr char CameraTransformEntrypoint[] = "camera_transform";
inline constexpr char PositionXParam[] = "position_x";
inline constexpr char PositionYParam[] = "position_y";
inline constexpr char RotationDegreesParam[] = "rotation_degrees";
inline constexpr char OpacityParam[] = "opacity";

std::optional<double> numericParam(const projection::RenderEffectNode& node, const std::string& name) {
  for (const auto& param : node.payload.params.values) {
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
         node.payload.implementation.entrypoint == CameraTransformEntrypoint;
}

foundation::Result<EffectPrepareResult> BuiltinEffectRuntime::prepare(const EffectPrepareRequest& request) {
  std::vector<RuntimeDiagnostic> diagnostics;
  const std::optional<double> positionX = numericParam(request.node, PositionXParam);
  const std::optional<double> positionY = numericParam(request.node, PositionYParam);

  if (!positionX.has_value()) {
    diagnostics.push_back(makeParamDiagnostic(request, PositionXParam));
  }
  if (!positionY.has_value()) {
    diagnostics.push_back(makeParamDiagnostic(request, PositionYParam));
  }

  RuntimeValueMap preparedValues;
  if (positionX.has_value() && positionY.has_value()) {
    preparedValues.push_back(RuntimeNamedValue{PositionXParam, RuntimeValue{*positionX}});
    preparedValues.push_back(RuntimeNamedValue{PositionYParam, RuntimeValue{*positionY}});
    preparedValues.push_back(RuntimeNamedValue{
      RotationDegreesParam,
      RuntimeValue{numericParam(request.node, RotationDegreesParam).value_or(0.0)}
    });
    preparedValues.push_back(RuntimeNamedValue{
      OpacityParam,
      RuntimeValue{numericParam(request.node, OpacityParam).value_or(1.0)}
    });
  }

  return EffectPrepareResult{
    PreparedEffectNode{
      request.graph.id,
      request.graph.targetNodeId,
      request.node.sourceNodeId,
      nullptr,
      std::move(preparedValues)
    },
    diagnostics
  };
}

foundation::Result<EffectProcessResult> BuiltinEffectRuntime::process(const EffectProcessRequest& request) {
  std::optional<double> positionX;
  std::optional<double> positionY;
  double rotationDegrees = 0.0;
  double opacity = 1.0;

  for (const RuntimeNamedValue& value : request.prepared.preparedValues) {
    const auto* numeric = std::get_if<double>(&value.value);
    if (numeric == nullptr) {
      continue;
    }
    if (value.name == PositionXParam) {
      positionX = *numeric;
    } else if (value.name == PositionYParam) {
      positionY = *numeric;
    } else if (value.name == RotationDegreesParam) {
      rotationDegrees = *numeric;
    } else if (value.name == OpacityParam) {
      opacity = *numeric;
    }
  }

  RuntimeValueMap outputValues;
  if (positionX.has_value() && positionY.has_value()) {
    outputValues.push_back(RuntimeNamedValue{
      output_name::CameraTransform,
      RuntimeValue{
        foundation::Transform2D{
          foundation::Vec2{*positionX, *positionY},
          foundation::Vec2{1.0, 1.0},
          rotationDegrees,
          opacity
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
