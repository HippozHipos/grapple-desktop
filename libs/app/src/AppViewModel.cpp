#include <grapple/app/AppViewModel.hpp>

#include <sstream>
#include <type_traits>
#include <variant>

namespace grapple::app {

std::string paramValueDisplayText(const timeline::ParamValue& value) {
  return std::visit(
    [](const auto& typedValue) -> std::string {
      using Value = std::decay_t<decltype(typedValue)>;
      std::ostringstream output;
      if constexpr (std::is_same_v<Value, double>) {
        output << typedValue;
      } else if constexpr (std::is_same_v<Value, bool>) {
        output << (typedValue ? "true" : "false");
      } else if constexpr (std::is_same_v<Value, std::string>) {
        output << typedValue;
      } else if constexpr (std::is_same_v<Value, foundation::Vec2>) {
        output << typedValue.x << ", " << typedValue.y;
      } else if constexpr (std::is_same_v<Value, foundation::Vec3>) {
        output << typedValue.x << ", " << typedValue.y << ", " << typedValue.z;
      } else if constexpr (std::is_same_v<Value, foundation::Rect>) {
        output << typedValue.x << ", " << typedValue.y << ", " << typedValue.width << "x" << typedValue.height;
      }
      return output.str();
    },
    value
  );
}

} // namespace grapple::app
