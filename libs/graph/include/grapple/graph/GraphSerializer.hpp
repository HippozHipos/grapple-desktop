#pragma once

#include <grapple/graph/GraphDocument.hpp>

#include <string>

namespace grapple::graph {

std::string serializeCanonicalGraph(const GraphDocument& graph);

} // namespace grapple::graph

