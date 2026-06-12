#pragma once

#include <compare>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace grapple::foundation {

template <typename Tag>
class StrongId {
public:
  StrongId() = default;

  explicit StrongId(std::string value)
    : value_(std::move(value)) {}

  [[nodiscard]] const std::string& value() const noexcept {
    return value_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return value_.empty();
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return !empty();
  }

  friend auto operator<=>(const StrongId&, const StrongId&) = default;

private:
  std::string value_;
};

template <typename Tag>
std::ostream& operator<<(std::ostream& stream, const StrongId<Tag>& id) {
  return stream << id.value();
}

struct ProjectIdTag;
struct RevisionIdTag;
struct NodeIdTag;
struct EdgeIdTag;
struct AssetIdTag;
struct CommandIdTag;
struct EventIdTag;
struct SnapshotIdTag;
struct GraphIdTag;
struct RunIdTag;
struct ToolIdTag;
struct ModelIdTag;
struct JobIdTag;

using ProjectId = StrongId<ProjectIdTag>;
using RevisionId = StrongId<RevisionIdTag>;
using NodeId = StrongId<NodeIdTag>;
using EdgeId = StrongId<EdgeIdTag>;
using AssetId = StrongId<AssetIdTag>;
using CommandId = StrongId<CommandIdTag>;
using EventId = StrongId<EventIdTag>;
using SnapshotId = StrongId<SnapshotIdTag>;
using GraphId = StrongId<GraphIdTag>;
using RunId = StrongId<RunIdTag>;
using ToolId = StrongId<ToolIdTag>;
using ModelId = StrongId<ModelIdTag>;
using JobId = StrongId<JobIdTag>;

} // namespace grapple::foundation
