#pragma once

#include <vector>

#include "util/string.h"

namespace path {

std::string normalize(const std::string& path) {
  const auto ends_with_slash = string::ends_with(path, "/");

  std::vector<std::string> result_parts;

  for (auto&& part : string::split<std::vector<std::string>>(path, '/')) {
    if (part == ".") continue;
    if (!result_parts.empty() && part.empty()) continue;
    if (part == "..") {
      if (!result_parts.empty()) result_parts.pop_back();
    } else {
      result_parts.emplace_back(std::move(part));
    }
  }

  if (ends_with_slash) result_parts.emplace_back();

  return string::join(result_parts, '/');
}

}  // namespace
