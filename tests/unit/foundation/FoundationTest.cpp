#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>

#include <TestAssert.hpp>

#include <string>

int main() {
  using namespace grapple::foundation;

  const ProjectId projectId{"proj_test"};
  const RevisionId revisionId{"rev_test"};
  GRAPPLE_REQUIRE(projectId.value() == "proj_test");
  GRAPPLE_REQUIRE(revisionId.value() == "rev_test");
  GRAPPLE_REQUIRE(projectId);

  const TimeRange range{TimeSeconds{1.0}, TimeSeconds{3.5}};
  GRAPPLE_REQUIRE(range.duration() == 2.5);
  GRAPPLE_REQUIRE(range.contains(TimeSeconds{1.0}));
  GRAPPLE_REQUIRE(!range.contains(TimeSeconds{3.5}));

  const Rect rect{10.0, 20.0, 100.0, 50.0};
  GRAPPLE_REQUIRE((rect.center() == Vec2{60.0, 45.0}));

  const Hash256 first = stableHash("graph");
  const Hash256 second = stableHash("graph");
  const Hash256 third = stableHash("other");
  GRAPPLE_REQUIRE(first == second);
  GRAPPLE_REQUIRE(!(first == third));
  GRAPPLE_REQUIRE(first.toHex().size() == 64);

  const Result<std::string> ok{std::string{"value"}};
  GRAPPLE_REQUIRE(ok);
  GRAPPLE_REQUIRE(ok.value() == "value");

  const Result<std::string> error{Error{"test_error", "failed"}};
  GRAPPLE_REQUIRE(!error);
  GRAPPLE_REQUIRE(error.error().code == "test_error");

  return 0;
}
