#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <string>

namespace grapple::project {

class IProjectIdAllocator {
public:
  virtual ~IProjectIdAllocator() = default;

  [[nodiscard]] virtual foundation::CommandId nextCommandId(const std::string& stem) = 0;
  [[nodiscard]] virtual foundation::AssetId nextAssetId(const std::string& stem) = 0;
  [[nodiscard]] virtual foundation::NodeId nextNodeId(const std::string& stem) = 0;
  [[nodiscard]] virtual foundation::EdgeId nextEdgeId(const std::string& stem) = 0;
};

} // namespace grapple::project
