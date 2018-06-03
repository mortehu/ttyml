#pragma once

#include <cctype>
#include <sstream>

namespace string {

char ascii_tolower(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch + 'a' - 'A';
  return ch;
}

void ascii_tolower(std::string* s) {
  for (auto& ch : *s) ch = ascii_tolower(ch);
}

template <typename T>
void cat_to(std::ostream* output, const T& t) {
  *output << t;
}

template <typename T, typename... Args>
void cat_to(std::ostream* output, const T& t, const Args&... args) {
  *output << t;
  cat_to(output, args...);
}

template <typename... Args>
std::string cat(const Args&... args) {
  std::ostringstream result;
  cat_to(&result, args...);
  return result.str();
}

template <typename Container>
void split_to(Container* result, const std::string& s, char delimiter) {
  std::string::size_type i = 0;

  for (;;) {
    const auto pos = s.find(delimiter, i);
    if (pos == std::string::npos) {
      result->emplace_back(s.data() + i, s.size() - i);
      break;
    }

    result->emplace_back(s.data() + i, pos - i);

    i = pos + 1;
  }
}

template <typename Container>
Container split(const std::string& s, char delimiter) {
  Container result;
  split_to(&result, s, delimiter);
  return result;
}

template <typename Container>
std::string join(const Container& parts, char delimiter) {
  std::string result;
  bool first = true;

  for (const auto& part : parts) {
    if (!first)
      result += delimiter;
    result += part;
    first = false;
  }

  return result;
}

bool starts_with(const std::string& haystack, const std::string& needle) {
  if (needle.size() > haystack.size()) return false;
  return 0 == haystack.compare(0, needle.size(), needle);
}

bool ends_with(const std::string& haystack, const std::string& needle) {
  if (needle.size() > haystack.size()) return false;
  return 0 == haystack.compare(haystack.size() - needle.size(), needle.size(), needle);
}

void strip_left(std::string* s) {
  std::string::size_type i = 0;
  while (i != s->size() && std::isspace(i)) ++i;
  if (i > 0) s->erase(0, i);
}

void strip_right(std::string* s) {
  while (!s->empty() && std::isspace(s->back())) s->pop_back();
}

void strip(std::string* s) {
  strip_left(s);
  strip_right(s);
}

}  // namespace string
