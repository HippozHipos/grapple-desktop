#include <grapple/runtime/BuiltinEffectRuntime.hpp>

#include <grapple/effects/BuiltinEffects.hpp>
#include <grapple/effects/OutputNames.hpp>

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

std::optional<foundation::Vec3> vec3Param(const RuntimeParamSet& params, const std::string& name) {
  for (const RuntimeParam& param : params) {
    if (param.name == name) {
      const auto* value = std::get_if<foundation::Vec3>(&param.value);
      if (value != nullptr) {
        return *value;
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

RuntimeDiagnostic makeCameraTransformParamDiagnostic(
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

RuntimeDiagnostic makeClipTintParamDiagnostic(
  const EffectPrepareRequest& request,
  const std::string& paramName
) {
  return RuntimeDiagnostic{
    "runtime.builtin_clip_tint_param_invalid",
    DiagnosticSeverity::Error,
    DiagnosticLocation{
      request.projectId,
      request.revision,
      request.node.sourceNodeId
    },
    "Builtin clip_tint effect requires parameter " + paramName + "."
  };
}

RuntimeDiagnostic makeClipExposureParamDiagnostic(
  const EffectPrepareRequest& request,
  const std::string& paramName
) {
  return RuntimeDiagnostic{
    "runtime.builtin_clip_exposure_param_invalid",
    DiagnosticSeverity::Error,
    DiagnosticLocation{
      request.projectId,
      request.revision,
      request.node.sourceNodeId
    },
    "Builtin clip_exposure effect requires numeric parameter " + paramName + "."
  };
}

bool isCameraTransform(const projection::RenderEffectNode& node) {
  return node.payload.implementation.kind == timeline::EffectImplementationKind::Builtin &&
         node.payload.implementation.entrypoint == effects::builtin_effect::CameraTransformEntrypoint;
}

bool isClipTint(const projection::RenderEffectNode& node) {
  return node.payload.implementation.kind == timeline::EffectImplementationKind::Builtin &&
         node.payload.implementation.entrypoint == effects::builtin_effect::ClipTintEntrypoint;
}

bool isClipExposure(const projection::RenderEffectNode& node) {
  return node.payload.implementation.kind == timeline::EffectImplementationKind::Builtin &&
         node.payload.implementation.entrypoint == effects::builtin_effect::ClipExposureEntrypoint;
}

foundation::Result<EffectPrepareResult> prepareCameraTransform(const EffectPrepareRequest& request) {
  std::vector<RuntimeDiagnostic> diagnostics;
  const RuntimeParamSet params = runtimeParamsFromEffectNode(request.node);
  const std::optional<double> positionX = numericParam(params, effects::builtin_effect::PositionXParam);
  const std::optional<double> positionY = numericParam(params, effects::builtin_effect::PositionYParam);
  const std::optional<double> zoom = numericParam(params, effects::builtin_effect::ZoomParam);

  if (!positionX.has_value()) {
    diagnostics.push_back(makeCameraTransformParamDiagnostic(request, effects::builtin_effect::PositionXParam));
  }
  if (!positionY.has_value()) {
    diagnostics.push_back(makeCameraTransformParamDiagnostic(request, effects::builtin_effect::PositionYParam));
  }
  if (!zoom.has_value()) {
    diagnostics.push_back(makeCameraTransformParamDiagnostic(request, effects::builtin_effect::ZoomParam));
  }

  RuntimeValueMap preparedValues;
  if (positionX.has_value() && positionY.has_value() && zoom.has_value()) {
    preparedValues.push_back(RuntimeNamedValue{effects::builtin_effect::PositionXParam, RuntimeValue{*positionX}});
    preparedValues.push_back(RuntimeNamedValue{effects::builtin_effect::PositionYParam, RuntimeValue{*positionY}});
    preparedValues.push_back(RuntimeNamedValue{effects::builtin_effect::ZoomParam, RuntimeValue{*zoom}});
  }

  return EffectPrepareResult{
    PreparedEffectNode{
      request.graph.id,
      request.graph.targetNodeId,
      request.node.sourceNodeId,
      request.node.payload.activeRange,
      nullptr,
      params,
      std::move(preparedValues),
      effects::builtin_effect::CameraTransformEntrypoint
    },
    diagnostics
  };
}

foundation::Result<EffectPrepareResult> prepareClipTint(const EffectPrepareRequest& request) {
  std::vector<RuntimeDiagnostic> diagnostics;
  const RuntimeParamSet params = runtimeParamsFromEffectNode(request.node);
  const std::optional<foundation::Vec3> color = vec3Param(params, effects::builtin_effect::ClipTintColorParam);
  const std::optional<double> amount = numericParam(params, effects::builtin_effect::ClipTintAmountParam);

  if (!color.has_value()) {
    diagnostics.push_back(makeClipTintParamDiagnostic(request, effects::builtin_effect::ClipTintColorParam));
  }
  if (!amount.has_value()) {
    diagnostics.push_back(makeClipTintParamDiagnostic(request, effects::builtin_effect::ClipTintAmountParam));
  }

  RuntimeValueMap preparedValues;
  if (color.has_value() && amount.has_value()) {
    preparedValues.push_back(RuntimeNamedValue{effects::builtin_effect::ClipTintColorParam, RuntimeValue{*color}});
    preparedValues.push_back(RuntimeNamedValue{effects::builtin_effect::ClipTintAmountParam, RuntimeValue{*amount}});
  }

  return EffectPrepareResult{
    PreparedEffectNode{
      request.graph.id,
      request.graph.targetNodeId,
      request.node.sourceNodeId,
      request.node.payload.activeRange,
      nullptr,
      params,
      std::move(preparedValues),
      effects::builtin_effect::ClipTintEntrypoint
    },
    diagnostics
  };
}

foundation::Result<EffectPrepareResult> prepareClipExposure(const EffectPrepareRequest& request) {
  std::vector<RuntimeDiagnostic> diagnostics;
  const RuntimeParamSet params = runtimeParamsFromEffectNode(request.node);
  const std::optional<double> exposure = numericParam(params, effects::builtin_effect::ClipExposureParam);

  if (!exposure.has_value()) {
    diagnostics.push_back(makeClipExposureParamDiagnostic(request, effects::builtin_effect::ClipExposureParam));
  }

  RuntimeValueMap preparedValues;
  if (exposure.has_value()) {
    preparedValues.push_back(RuntimeNamedValue{effects::builtin_effect::ClipExposureParam, RuntimeValue{*exposure}});
  }

  return EffectPrepareResult{
    PreparedEffectNode{
      request.graph.id,
      request.graph.targetNodeId,
      request.node.sourceNodeId,
      request.node.payload.activeRange,
      nullptr,
      params,
      std::move(preparedValues),
      effects::builtin_effect::ClipExposureEntrypoint
    },
    diagnostics
  };
}

} // namespace

bool BuiltinEffectRuntime::supports(const projection::RenderEffectNode& node) const {
  return isCameraTransform(node) || isClipTint(node) || isClipExposure(node);
}

foundation::Result<EffectPrepareResult> BuiltinEffectRuntime::prepare(const EffectPrepareRequest& request) {
  if (isCameraTransform(request.node)) {
    return prepareCameraTransform(request);
  }
  if (isClipExposure(request.node)) {
    return prepareClipExposure(request);
  }
  return prepareClipTint(request);
}

foundation::Result<EffectProcessResult> BuiltinEffectRuntime::process(const EffectProcessRequest& request) {
  if (request.prepared.preparedValues.empty()) {
    return EffectProcessResult{
      RuntimeEffectOutput{
        request.prepared.effectGraphId,
        request.prepared.targetNodeId,
        request.prepared.sourceNodeId,
        {}
      },
      {}
    };
  }

  if (request.prepared.entrypoint == effects::builtin_effect::ClipTintEntrypoint) {
    std::optional<foundation::Vec3> color;
    std::optional<double> amount;
    for (const RuntimeNamedValue& value : request.params) {
      if (value.name == effects::builtin_effect::ClipTintColorParam) {
        if (const auto* vector = std::get_if<foundation::Vec3>(&value.value)) {
          color = *vector;
        }
      } else if (value.name == effects::builtin_effect::ClipTintAmountParam) {
        if (const auto* numeric = std::get_if<double>(&value.value)) {
          amount = *numeric;
        }
      }
    }

    RuntimeValueMap outputValues;
    if (color.has_value() && amount.has_value()) {
      outputValues.push_back(RuntimeNamedValue{effects::output_name::ClipTint, RuntimeValue{*color}});
      outputValues.push_back(RuntimeNamedValue{effects::output_name::ClipTintAmount, RuntimeValue{*amount}});
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

  if (request.prepared.entrypoint == effects::builtin_effect::ClipExposureEntrypoint) {
    std::optional<double> exposure;
    for (const RuntimeNamedValue& value : request.params) {
      if (value.name != effects::builtin_effect::ClipExposureParam) {
        continue;
      }
      if (const auto* numeric = std::get_if<double>(&value.value)) {
        exposure = *numeric;
      }
    }

    RuntimeValueMap outputValues;
    if (exposure.has_value()) {
      outputValues.push_back(RuntimeNamedValue{effects::output_name::ClipExposure, RuntimeValue{*exposure}});
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

  std::optional<double> positionX;
  std::optional<double> positionY;
  std::optional<double> zoom;

  for (const RuntimeNamedValue& value : request.params) {
    const auto* numeric = std::get_if<double>(&value.value);
    if (numeric == nullptr) {
      continue;
    }
    if (value.name == effects::builtin_effect::PositionXParam) {
      positionX = *numeric;
    } else if (value.name == effects::builtin_effect::PositionYParam) {
      positionY = *numeric;
    } else if (value.name == effects::builtin_effect::ZoomParam) {
      zoom = *numeric;
    }
  }

  RuntimeValueMap outputValues;
  if (positionX.has_value() && positionY.has_value() && zoom.has_value()) {
    outputValues.push_back(RuntimeNamedValue{
      effects::output_name::CameraTransform,
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
