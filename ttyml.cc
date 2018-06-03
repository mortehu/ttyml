#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ttyml.h"

#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <curl/curl.h>
#include <expat.h>
#include <readline/readline.h>

#include "util/curl.h"
#include "util/string.h"
#include "util/tty.h"
#include "util/url.h"

#define NS_PREFIX "https://ttyml.org/2018/05/26|"

#define CHECK_EXPAT(call)                                                     \
  do {                                                                        \
    const auto ret = (call);                                                  \
    if (ret == XML_STATUS_OK) break;                                          \
    throw std::runtime_error{string::cat(                                     \
        "line ", XML_GetCurrentLineNumber(xml_parser_.get()), ", column ",    \
        XML_GetCurrentColumnNumber(xml_parser_.get()), ", offset ",           \
        XML_GetCurrentByteIndex(xml_parser_.get()), ": ",                     \
        XML_ErrorString(XML_GetErrorCode(xml_parser_.get())), "\nCall was: ", \
        #call)};                                                              \
  } while (0);

namespace ttyml {

const std::unordered_map<std::string, Context::Element>
    Context::tag_to_element_s{{
        {NS_PREFIX "form", Element::Form},
        {NS_PREFIX "line", Element::Line},
        {NS_PREFIX "prompt", Element::Prompt},
        {NS_PREFIX "style", Element::Style},
        {NS_PREFIX "ttyml", Element::Root},
        {NS_PREFIX "var", Element::Var},
    }};

unsigned int parse_color(const char* value) {
  if (!value || 0 == std::strcmp(value, "default")) return 9;

  char* endptr = nullptr;
  errno = 0;
  const auto result = std::strtoul(value, &endptr, 10);
  if (errno != 0) {
    throw std::runtime_error{
        string::cat("invalid color attribute '", value, "'")};
  }

  return result;
}

Context::Context(const char* url, const char* method, const char* data)
    : url_{url},
      curl_{curl_easy_init(), curl_easy_cleanup},
      xml_parser_{nullptr, XML_ParserFree},
      action_{url} {
  if (!curl_) throw std::runtime_error{"curl_easy_init() failed"};

  curl::string_list headers;
  headers.append("Accept: text/ttyml");

#ifdef HAVE_SYS_IOCTL_H
  winsize stdout_winsize;
  memset(&stdout_winsize, 0, sizeof(stdout_winsize));
  if (-1 != ioctl(STDOUT_FILENO, TIOCGWINSZ, &stdout_winsize)) {
    if (stdout_winsize.ws_col > 0)
      headers.append(string::cat("Tty-Columns: ", stdout_winsize.ws_col));
    if (stdout_winsize.ws_row > 0)
      headers.append(string::cat("Tty-Lines: ", stdout_winsize.ws_row));
  }
#endif

  curl::setopt(curl_.get(), CURLOPT_URL, url);
  curl::setopt(curl_.get(), CURLOPT_ACCEPT_ENCODING, "gzip,deflate");
  curl::setopt(curl_.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
  curl::setopt(curl_.get(), CURLOPT_HTTPHEADER, headers.get());
  curl::setopt(curl_.get(), CURLOPT_HEADERDATA, this);
  curl::setopt(curl_.get(), CURLOPT_HEADERFUNCTION,
               +[](const void* ptr, size_t size, size_t nmemb,
                   void* void_context) -> size_t {
                 const auto context = static_cast<Context*>(void_context);
                 if (!context->wrap_exception(
                         [=] { context->put_header(ptr, size * nmemb); }))
                   return 0;
                 return nmemb;
               });

  curl::setopt(curl_.get(), CURLOPT_WRITEDATA, this);
  curl::setopt(
      curl_.get(), CURLOPT_WRITEFUNCTION,
      +[](const void* ptr, size_t size, size_t nmemb,
          void* void_context) -> size_t {
        const auto context = static_cast<Context*>(void_context);
        if (!context->wrap_exception([=] { context->put(ptr, size * nmemb); }))
          return 0;
        return nmemb;
      });

  const auto curl_ret = curl_easy_perform(curl_.get());
  if (pending_exception_) std::rethrow_exception(pending_exception_);
  if (curl_ret != CURLE_OK)
    throw std::runtime_error{string::cat("curl_easy_perform failed: ",
                                         curl_easy_strerror(curl_ret))};

  if (xml_parser_) {
    XML_Parse(xml_parser_.get(), nullptr, 0, 1);
    if (pending_exception_) std::rethrow_exception(pending_exception_);
  }
}

std::unique_ptr<Context> Context::next_context() const {
  if (prompts_.empty()) return nullptr;

  // Loop until we get a valid result.
  for (;;) {
    std::string data;

    for (const auto& var : vars_)
      url::append_key_value(&data, var.first, var.second);

    for (const auto& prompt : prompts_) {
      // Loop until we get valid input.
      for (;;) {
        std::unique_ptr<char[], decltype(&free)> value_buf{
            readline(prompt.prompt_.c_str()), free};
        if (!value_buf) return nullptr;

        std::string value{value_buf.get()};
        string::strip(&value);

        if (!std::regex_match(value, prompt.filter_regex_)) {
          if (!value.empty()) {
            if (!prompt.filter_message_.empty()) {
              std::cerr << prompt.filter_message_ << '\n';
            } else {
              std::cerr << "Invalid input.  Must match '"
                        << prompt.filter_regex_str_ << "'\n";
            }
          }

          continue;
        }

        url::append_key_value(&data, prompt.name_, value);

        break;
      }
    }

    try {
      auto url = url::normalize(action_, url_);

      if (method_ != "POST" && !data.empty()) {
        const auto q = url.find('?');
        if (q != std::string::npos) url.erase(q);
        url.push_back('?');
        url.append(data);
        data.clear();
      }

      return std::make_unique<Context>(url.c_str(), method_.c_str(),
                                       data.c_str());
    } catch (std::runtime_error& e) {
      std::cerr << "Error: " << e.what() << '\n';
      continue;
    }
  }
}

void Context::put_header(const void* buf, size_t size) {
  std::string line{static_cast<const char*>(buf), size};
  string::strip_right(&line);
  if (line.empty()) return;

  if (status_message_.empty()) {
    int status_offset = 0;

    if (3 != sscanf(line.c_str(), "HTTP/%u.%u %u %n", &http_version_major_,
                    &http_version_minor_, &status_code_, &status_offset)) {
      throw std::runtime_error{
          string::cat("invalid status header: '", line, "'")};
    }

    status_message_ = line.substr(status_offset);

    return;
  }

  std::string::size_type i;
  std::string key;

  for (i = 0; i < line.size(); ++i) {
    if (line[i] == ':') {
      ++i;
      break;
    }

    key.push_back(std::tolower(line[i]));
  }

  while (i < line.size() && std::isspace(line[i])) ++i;

  if (key == "content-type") {
    auto data = string::split<std::vector<std::string>>(line.substr(i), ';');
    for (auto& d : data) string::strip(&d);
    if (data.empty()) return;

    mime_type_ = data[0];
    string::ascii_tolower(&mime_type_);

    for (size_t i = 1; i < data.size(); ++i) {
      string::ascii_tolower(&data[i]);
      if (string::starts_with(data[i], "charset="))
        charset_ = data[i].substr(8);
    }

    if (mime_type_ != "text/ttyml") {
      throw std::runtime_error{
          string::cat("server responded with unsupported content type '",
                      line.substr(i), "'")};
    }
  }
}

void Context::put(const void* buf, size_t size) {
  if (!xml_parser_) {
    xml_parser_.reset(XML_ParserCreateNS(charset_.c_str(), '|'));
    if (!xml_parser_)
      throw std::runtime_error{"XML_ParserCreate returned NULL"};

    CHECK_EXPAT(XML_SetBase(xml_parser_.get(), url_.c_str()));

    XML_SetUserData(xml_parser_.get(), this);

    XML_SetElementHandler(
        xml_parser_.get(),
        +[](void* user_data, const XML_Char* name, const XML_Char** atts) {
          const auto context = static_cast<Context*>(user_data);
          context->wrap_exception([=] { context->start_element(name, atts); });
        },
        +[](void* user_data, const XML_Char* name) {
          const auto context = static_cast<Context*>(user_data);
          context->wrap_exception([=] { context->end_element(name); });
        });

    XML_SetCharacterDataHandler(
        xml_parser_.get(), +[](void* user_data, const XML_Char* s, int len) {
          const auto context = static_cast<Context*>(user_data);
          context->wrap_exception([=] { context->character_data(s, len); });
        });
  }

  CHECK_EXPAT(
      XML_Parse(xml_parser_.get(), static_cast<const char*>(buf), size, 0));
}

void Context::start_element(const XML_Char* name, const XML_Char** atts) {
  const auto element_it = tag_to_element_s.find(name);
  auto out_element = Element::Unknown;
  if (element_it != tag_to_element_s.end()) {
    switch (element_it->second) {
      case Element::Form:
        if (!stack_.empty() && stack_.back() == Element::Root) {
          out_element = Element::Form;

          for (size_t attr_idx = 0; atts[attr_idx]; attr_idx += 2) {
            const auto attr_name = atts[attr_idx];
            const auto attr_value = atts[attr_idx + 1];

            if (0 == std::strcmp(attr_name, "action"))
              action_.assign(attr_value);
            else if (0 == std::strcmp(attr_name, "method"))
              method_.assign(attr_value);
          }
        }
        break;

      case Element::Line:
        if (!stack_.empty() && stack_.back() == Element::Root) {
          out_element = Element::Line;
          writer_stack_.emplace_back(std::make_unique<tty::StdoutWriter>());
        }
        break;

      case Element::Prompt:
        if (!stack_.empty() && stack_.back() == Element::Form) {
          out_element = Element::Prompt;

          const char* name = nullptr;
          const char* filter_regex = nullptr;
          const char* filter_message = nullptr;

          for (size_t attr_idx = 0; atts[attr_idx]; attr_idx += 2) {
            const auto attr_name = atts[attr_idx];
            const auto attr_value = atts[attr_idx + 1];

            if (0 == std::strcmp(attr_name, "filter-regex"))
              filter_regex = attr_value;
            else if (0 == std::strcmp(attr_name, "filter-message"))
              filter_message = attr_value;
            else if (0 == std::strcmp(attr_name, "name"))
              name = attr_value;
          }

          if (!name) {
            throw std::runtime_error{
                "prompt element is missing name attribute"};
          }

          prompts_.emplace_back(name);

          auto& prompt = prompts_.back();

          if (filter_regex) {
            prompt.filter_regex_str_.assign(filter_regex);
            prompt.filter_regex_.assign(prompt.filter_regex_str_);
          }

          if (filter_message) prompt.filter_message_.assign(filter_message);

          writer_stack_.emplace_back(
              std::make_unique<tty::PromptWriter>(prompt.prompt_));
        }
        break;

      case Element::Root:
        if (stack_.empty()) out_element = Element::Root;
        break;

      case Element::Style:
        if (!writer_stack_.empty()) {
          auto& writer = *writer_stack_.back();

          out_element = Element::Style;

          auto new_style = writer.style_stack_.back();

          for (size_t attr_idx = 0; atts[attr_idx]; attr_idx += 2) {
            const auto attr_name = atts[attr_idx];
            const auto attr_value = atts[attr_idx + 1];

            if (0 == std::strcmp(attr_name, "bg")) {
              new_style.bg_ = parse_color(attr_value);
            } else if (0 == std::strcmp(attr_name, "bold")) {
              if (0 == std::strcmp(attr_value, "0")) {
                new_style.bold_ = false;
              } else if (0 == std::strcmp(attr_value, "1")) {
                new_style.bold_ = true;
              } else {
                throw std::runtime_error{
                    string::cat("invalid bold attribute '", attr_value, "'")};
              }
            } else if (0 == std::strcmp(attr_name, "fg")) {
              new_style.fg_ = parse_color(attr_value);
            }
          }

          writer.transition(writer.style_stack_.back(), new_style);
          writer.style_stack_.emplace_back(new_style);
        }
        break;

      case Element::Var: {
        const char* name = nullptr;
        const char* value = nullptr;

        for (size_t attr_idx = 0; atts[attr_idx]; attr_idx += 2) {
          const auto attr_name = atts[attr_idx];
          const auto attr_value = atts[attr_idx + 1];

          if (0 == std::strcmp(attr_name, "name"))
            name = attr_value;
          else if (0 == std::strcmp(attr_name, "value"))
            value = attr_value;
        }

        if (!name)
          throw std::runtime_error{"var element is missing name attribute"};
        if (!value)
          throw std::runtime_error{"var element is missing value attribute"};

        vars_.emplace_back(name, value);
      } break;

      case Element::Unknown:
        break;
    }
  }

  stack_.emplace_back(out_element);
}

void Context::end_element(const XML_Char* name) {
  if (stack_.empty()) throw std::logic_error{"unexpected end element call"};
  switch (stack_.back()) {
    case Element::Line:
      writer_stack_.pop_back();
      std::cout << std::endl;
      if (std::cout.bad())
        throw std::runtime_error{"write to standard output failed"};
      break;

    case Element::Style: {
      auto& writer = *writer_stack_.back();
      writer.transition(writer.style_stack_.back(),
                        writer.style_stack_[writer.style_stack_.size() - 2]);
      writer.style_stack_.pop_back();
    } break;

    case Element::Prompt:
      writer_stack_.pop_back();
      break;

    case Element::Form:
    case Element::Root:
    case Element::Var:
    case Element::Unknown:
      break;
  }
  stack_.pop_back();
}

void Context::character_data(const XML_Char* s, int len) {
  if (stack_.empty()) return;

  switch (stack_.back()) {
    case Element::Line:
    case Element::Prompt:
    case Element::Style:
      writer_stack_.back()->put(s, len);
      break;

    case Element::Form:
    case Element::Root:
    case Element::Var:
    case Element::Unknown:
      break;
  }
}

}  // namespace ttyml
