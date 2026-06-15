#pragma once

#include <grapple/foundation/StrongId.hpp>

namespace grapple::runtime {

struct ScriptModuleHandleTag;
struct ShaderProgramHandleTag;
struct TextureHandleTag;

using ScriptModuleHandle = foundation::StrongId<ScriptModuleHandleTag>;
using ShaderProgramHandle = foundation::StrongId<ShaderProgramHandleTag>;
using TextureHandle = foundation::StrongId<TextureHandleTag>;

} // namespace grapple::runtime
