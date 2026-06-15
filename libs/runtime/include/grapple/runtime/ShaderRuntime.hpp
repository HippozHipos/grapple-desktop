#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/runtime/RuntimeHandle.hpp>
#include <grapple/runtime/RuntimeOutput.hpp>
#include <grapple/runtime/RuntimeQuality.hpp>
#include <grapple/runtime/RuntimeSource.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

enum class ShaderBindingKind {
  Value,
  Texture
};

struct ShaderBinding {
  std::string name;
  ShaderBindingKind kind = ShaderBindingKind::Value;

  friend bool operator==(const ShaderBinding&, const ShaderBinding&) = default;
};

struct ShaderBindingLayout {
  std::vector<ShaderBinding> inputs;
  std::vector<ShaderBinding> outputs;

  friend bool operator==(const ShaderBindingLayout&, const ShaderBindingLayout&) = default;
};

struct ShaderExecutionInputs {
  RuntimeValueMap values;
  RuntimeQuality quality = RuntimeQuality::Interactive;
};

class IShaderRuntime {
public:
  virtual ~IShaderRuntime() = default;

  virtual foundation::Result<ShaderProgramHandle> compile(
    const RuntimeSource& source,
    const ShaderBindingLayout& layout
  ) = 0;

  virtual foundation::Result<TextureHandle> execute(
    const ShaderProgramHandle& program,
    const ShaderExecutionInputs& inputs
  ) = 0;
};

} // namespace grapple::runtime
