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

const std::unordered_map<std::string, Context::Element> Context::tag_to_element_s{{
  {NS_PREFIX "line", Element::Line},
  {NS_PREFIX "prompt", Element::Prompt},
  {NS_PREFIX "ttyml", Element::Root},
}};

Context::Context(const char* url)
    : url_{url},
      curl_{curl_easy_init(), curl_easy_cleanup},
      xml_parser_{nullptr, XML_ParserFree} {
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
                 const auto context = static_cast<Context *>(void_context);
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
        const auto context = static_cast<Context *>(void_context);
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

  if (has_prompt_) {
    std::unique_ptr<char[], decltype(&free)> command{readline(prompt_.c_str()), free};
  }
}

void Context::put_header(const void* buf, size_t size) {
  std::string line{static_cast<const char *>(buf), size};
  string::strip_right(line);
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
    for (auto &d : data) string::strip(d);
    if (data.empty()) return;

    mime_type_ = data[0];
    string::to_lower(mime_type_);

    for (size_t i = 1; i < data.size(); ++i) {
      string::to_lower(data[i]);
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
        +[](void* user_data, const XML_Char* name, const XML_Char **atts) {
          const auto context = static_cast<Context*>(user_data);
          context->wrap_exception([=] { context->start_element(name, atts); });
        },
        +[](void* user_data, const XML_Char* name) {
          const auto context = static_cast<Context*>(user_data);
          context->wrap_exception([=] { context->end_element(name); });
        });

    XML_SetCharacterDataHandler(
        xml_parser_.get(),
        +[](void* user_data, const XML_Char* s, int len) {
          const auto context = static_cast<Context*>(user_data);
          context->wrap_exception([=] { context->character_data(s, len); });
        });
  }

  CHECK_EXPAT(
      XML_Parse(xml_parser_.get(), static_cast<const char *>(buf), size, 0));
}

void Context::start_element(const XML_Char* name, const XML_Char** atts) {
  const auto element_it = tag_to_element_s.find(name);
  auto out_element = Element::Unknown;
  if (element_it != tag_to_element_s.end()) {
    switch (element_it->second) {
    case Element::Root:
      if (stack_.empty()) out_element = Element::Root;
      break;

    case Element::Line:
      if (!stack_.empty() && stack_.back() == Element::Root)
        out_element = Element::Line;
      break;

    case Element::Prompt:
      if (has_prompt_) throw std::runtime_error{"multiple prompt elements"};
      if (!stack_.empty() && stack_.back() == Element::Root) {
        for (size_t attr_idx = 0; atts[attr_idx]; attr_idx += 2) {
          if (0 == std::strcmp(atts[attr_idx], "action"))
            action_ = atts[attr_idx + 1];
          else if (0 == std::strcmp(atts[attr_idx], "method"))
            method_ = atts[attr_idx + 1];
        }
        out_element = Element::Prompt;
        has_prompt_ = true;
      }
      break;

    case Element::Unknown:
      break;
    }
  }

  stack_.emplace_back(out_element);
}

void Context::end_element(const XML_Char* name) {
  if (stack_.empty()) throw std::logic_error{"unexpected end element call"};
  stack_.pop_back();
}

void Context::character_data(const XML_Char* s, int len) {
  if (stack_.empty()) return;

  switch (stack_.back()) {
  case Element::Line:
    std::cout.write(s, len);
    std::cout << std::endl;
    if (std::cout.bad()) throw std::runtime_error{"write to standard output failed"};
    break;

  case Element::Prompt:
    prompt_.append(s, len);
    break;

  default:;
  }
}

}  // namespace ttyml
