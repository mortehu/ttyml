#pragma once

#include <cassert>
#include <iostream>

namespace tty {

struct Style {
  unsigned int fg_ = 9;
  unsigned int bg_ = 9;
  bool bold_ = false;

  bool operator==(const Style& rhs) const {
    return fg_ == rhs.fg_ && bg_ == rhs.bg_ && bold_ == rhs.bold_;
  }

  bool operator!=(const Style& rhs) const {
    return fg_ != rhs.fg_ || bg_ != rhs.bg_ || bold_ != rhs.bold_;
  }
};

class Writer {
 public:
  Writer() { style_stack_.emplace_back(); }

  virtual ~Writer() { assert(style_stack_.size() == 1); }

  virtual void put(const char* text, size_t len) = 0;

  virtual void transition(const Style& from, const Style& to) = 0;

  std::vector<tty::Style> style_stack_;
};

class StdoutWriter : public Writer {
 public:
  void put(const char* text, size_t len) final { std::cout.write(text, len); }

  void transition(const Style& from, const Style& to) final {
    if (from == to) return;

    bool first = true;

    std::cout << "\033[";

    if (to != Style{}) {
      if (from.bold_ != to.bold_) {
        std::cout << (to.bold_ ? "1" : "22");
        first = false;
      }

      if (from.fg_ != to.fg_ && to.fg_ <= 9) {
        if (!first) std::cout << ';';
        std::cout << (30 + to.fg_);
        first = false;
      }

      if (from.bg_ != to.bg_ && to.bg_ <= 9) {
        if (!first) std::cout << ';';
        std::cout << (40 + to.fg_);
        first = false;
      }
    }

    std::cout << 'm';
  }
};

class PromptWriter : public Writer {
 public:
  PromptWriter(std::string& buffer) : buffer_{buffer} {}

  void put(const char* text, size_t len) final { buffer_.append(text, len); }

  void transition(const Style& from, const Style& to) final {
    if (from == to) return;

    bool first = true;

    buffer_.append("\033[");

    if (to != Style{}) {
      if (from.bold_ != to.bold_) {
        buffer_.append(to.bold_ ? "1" : "22");
        first = false;
      }

      if (from.fg_ != to.fg_ && to.fg_ <= 9) {
        if (!first) buffer_.push_back(';');
        buffer_.append(std::to_string(30 + to.fg_));
        first = false;
      }

      if (from.bg_ != to.bg_ && to.bg_ <= 9) {
        if (!first) buffer_.push_back(';');
        buffer_.append(std::to_string(40 + to.fg_));
        first = false;
      }
    }

    buffer_.append("m");
  }

 private:
  std::string& buffer_;
};

}  // namespace tty
