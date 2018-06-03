#include "util/path.h"

#include "third_party/gtest/include/gtest/gtest.h"

namespace {

TEST(PathTest, Normalize) {
  EXPECT_EQ("/a/b/c/", path::normalize("/a/b/d/.././/c/"));
  EXPECT_EQ("a/b/c", path::normalize("a/b/d/.././/c"));
}

}  // namespace
