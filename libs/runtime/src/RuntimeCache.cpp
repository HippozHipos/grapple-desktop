#include <grapple/runtime/RuntimeCache.hpp>

#include <sstream>

namespace grapple::runtime {

namespace {

const RuntimeDependencyNode* findNode(
  const RuntimeDependencyGraph& graph,
  RuntimeDependencyId dependencyId
) {
  for (const RuntimeDependencyNode& node : graph.nodes) {
    if (node.id == dependencyId) {
      return &node;
    }
  }

  return nullptr;
}

void appendRange(std::ostringstream& stream, const foundation::TimeRange& range) {
  stream << range.start.value << ':' << range.end.value;
}

void appendNodeFingerprint(
  std::ostringstream& stream,
  const RuntimeDependencyGraph& graph,
  const RuntimeDependencyNode& node
) {
  stream << "node|" << node.id.value()
         << "|render|" << node.renderNodeId.value()
         << "|impl|" << node.implementationHash.toHex()
         << "|params|" << node.paramsHash.toHex()
         << "|range|";
  appendRange(stream, node.activeRange);

  stream << "|assets[";
  for (const RuntimeAssetDependency& asset : node.assetDependencies) {
    stream << asset.assetId.value() << ':' << asset.versionHash.toHex() << ';';
  }
  stream << "]|inputs[";
  for (RuntimeDependencyId inputDependency : node.inputDependencies) {
    const RuntimeDependencyNode* inputNode = findNode(graph, inputDependency);
    if (inputNode != nullptr) {
      appendNodeFingerprint(stream, graph, *inputNode);
    }
  }
  stream << ']';
}

} // namespace

bool operator==(const RuntimeCacheKey& left, const RuntimeCacheKey& right) {
  return left.projectId == right.projectId &&
         left.nodeId == right.nodeId &&
         left.implementationHash == right.implementationHash &&
         left.paramsHash == right.paramsHash &&
         left.inputsHash == right.inputsHash &&
         left.assetDependencies == right.assetDependencies &&
         left.range == right.range &&
         left.runtimeVersion == right.runtimeVersion;
}

foundation::Hash256 hashRuntimeCacheInputs(
  const RuntimeDependencyGraph& graph,
  const RuntimeDependencyNode& node
) {
  std::ostringstream stream;
  for (RuntimeDependencyId inputDependency : node.inputDependencies) {
    const RuntimeDependencyNode* inputNode = findNode(graph, inputDependency);
    if (inputNode != nullptr) {
      appendNodeFingerprint(stream, graph, *inputNode);
    }
  }
  return foundation::stableHash(stream.str());
}

RuntimeCacheKey runtimeCacheKeyForDependency(
  const RuntimeDependencyGraph& graph,
  const RuntimeDependencyNode& node,
  std::string_view runtimeVersion
) {
  return RuntimeCacheKey{
    graph.projectId,
    node.renderNodeId,
    node.implementationHash,
    node.paramsHash,
    hashRuntimeCacheInputs(graph, node),
    node.assetDependencies,
    node.activeRange,
    std::string{runtimeVersion}
  };
}

} // namespace grapple::runtime
