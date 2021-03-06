#pragma once

#include <exception>
#include <memory>
#include <regex>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>
#include <expat.h>

#include "util/tty.h"

namespace ttyml {

class Context {
 public:
  Context(const char* url, const char* method = "GET",
          const char* data = nullptr);

  bool has_prompt() const { return !prompts_.empty(); }

  std::unique_ptr<Context> next_context() const;

 private:
  enum class Element {
    Form,
    Line,
    Prompt,
    Root,
    Style,
    Var,

    Unknown,
  };

  struct Prompt {
    Prompt(std::string name) : name_{std::move(name)} {}

    const std::string name_;
    std::string prompt_;

    std::string filter_regex_str_;
    std::regex filter_regex_;

    std::string filter_message_;
  };

  static const std::unordered_map<std::string, Element> tag_to_element_s;

  std::string url_;

  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_;

  std::exception_ptr pending_exception_;

  unsigned int http_version_major_ = 1;
  unsigned int http_version_minor_ = 0;
  unsigned int status_code_ = 0;
  std::string status_message_;

  std::string mime_type_;
  std::string charset_ = "utf-8";

  std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)> xml_parser_;

  std::vector<Element> stack_;
  std::vector<std::unique_ptr<tty::Writer>> writer_stack_;

  std::vector<std::pair<std::string, std::string>> vars_;
  std::vector<Prompt> prompts_;
  std::string action_;
  std::string method_ = "GET";

  void put_header(const void* buf, size_t size);
  void put(const void* buf, size_t size);

  void start_element(const XML_Char* name, const XML_Char** atts);
  void end_element(const XML_Char* name);
  void character_data(const XML_Char* s, int len);

  template <typename Function>
  bool wrap_exception(Function&& f) {
    if (pending_exception_) return false;

    try {
      f();
    } catch (...) {
      if (!pending_exception_) pending_exception_ = std::current_exception();
      return false;
    }
    return true;
  }
};

}  // namespace ttyml
