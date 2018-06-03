#pragma once

#include <cctype>
#include <cstring>
#include <string>

#include "util/path.h"
#include "util/string.h"

namespace url {

// Represents the part of a URL.
struct parts {
  parts() = default;

  parts(std::string scheme, std::string host, std::string path,
        std::string fragment)
      : scheme{std::move(scheme)},
        host{std::move(host)},
        path{std::move(path)},
        fragment{std::move(fragment)} {}

  bool operator==(const parts& rhs) const {
    return scheme == rhs.scheme && host == rhs.host && path == rhs.path &&
           fragment == rhs.fragment;
  }

  std::string scheme;
  std::string host;  // May include user name and password.
  std::string path;  // May include query.
  std::string fragment;
};

inline void escape(std::string* output, const std::string& input) {
  static const char hex_digits[] = "0123456789ABCDEF";

  for (const char ch : input) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') || nullptr != std::strchr("-_()", ch)) {
      output->push_back(ch);
    } else {
      output->push_back('%');
      output->push_back(hex_digits[static_cast<unsigned char>(ch) >> 4]);
      output->push_back(hex_digits[static_cast<unsigned char>(ch) & 15]);
    }
  }
}

inline void append_key_value(std::string* output, const std::string& key,
                             const std::string& value) {
  if (!output->empty()) output->push_back('&');
  escape(output, key);
  output->push_back('=');
  escape(output, value);
}

// Parses a URL into its parts.
//
// If the path component is implicitly "/", like in <http://www.example.org>,
// the resulting object may contain a path even though the URL itself does not.
parts parse(const std::string& url) {
  parts result;

  std::string::size_type pos = 0;

  const auto scheme_end = url.find(':');
  if (scheme_end != std::string::npos) {
    result.scheme = url.substr(0, scheme_end + 1);
    pos = scheme_end + 1;

    for (auto& ch : result.scheme)
      ch = string::ascii_tolower(ch);
  }

  if (0 == url.compare(pos, 2, "//")) {
    const auto host_end = url.find('/', pos + 2);
    if (host_end == std::string::npos) {
      result.host = url.substr(pos);
      result.path = "/";

      const auto fragment_start = result.host.find('#');

      if (fragment_start != std::string::npos) {
        result.fragment = result.host.substr(fragment_start);
        result.host.erase(fragment_start);
      }

      return result;
    }

    result.host = url.substr(pos, host_end - pos);

    auto auth_end = result.host.find('@');
    if (auth_end == std::string::npos) auth_end = 0;

    for (auto i = auth_end; i != result.host.size(); ++i)
      result.host[i] = string::ascii_tolower(result.host[i]);

    pos = host_end;
  }

  result.path = url.substr(pos);

  const auto fragment_start = result.path.find('#');

  if (fragment_start != std::string::npos) {
    result.fragment = result.path.substr(fragment_start);
    result.path.erase(fragment_start);
  }

  return result;
}

// Computes the absolute URL from an optionally relative URL, and an absolute
// URL.
std::string normalize(const std::string& url, const std::string& base) {
  const auto base_parts = parse(base);
  auto url_parts = parse(url);

  if (url_parts.path.empty())
    return string::cat(base_parts.scheme, base_parts.host, base_parts.path,
                       url_parts.fragment);

  if (url_parts.path[0] != '/') {
    auto path = base_parts.path;
    path += '/';
    path += url_parts.path;

    if (string::ends_with(path, "/.") || string::ends_with(path, "/.."))
      path += '/';

    url_parts.path = path::normalize(path);
  }

  if (url_parts.host.empty())
    return string::cat(base_parts.scheme, base_parts.host, url_parts.path,
                       url_parts.fragment);

  if (url_parts.scheme.empty())
    return string::cat(base_parts.scheme, url_parts.host, url_parts.path,
                       url_parts.fragment);

  return url;
}

}  // namespace url
