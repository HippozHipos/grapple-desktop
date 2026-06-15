#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/runtime/RuntimeContext.hpp>
#include <grapple/runtime/RuntimeHandle.hpp>
#include <grapple/runtime/RuntimeOutput.hpp>
#include <grapple/runtime/RuntimeSource.hpp>

namespace grapple::runtime {

class IScriptRuntime {
public:
  virtual ~IScriptRuntime() = default;

  virtual foundation::Result<ScriptModuleHandle> compile(const RuntimeSource& source) = 0;

  virtual foundation::Result<RuntimeValueMap> callPrepare(
    const ScriptModuleHandle& module,
    RuntimeContext& context
  ) = 0;

  virtual foundation::Result<RuntimeValueMap> callProcess(
    const ScriptModuleHandle& module,
    RuntimeContext& context
  ) = 0;
};

} // namespace grapple::runtime
