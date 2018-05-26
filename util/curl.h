#pragma once

// Helper functions for dealing with the cURL library.

#include <iostream>

#include <curl/curl.h>

#include "util/string.h"

namespace curl {

class string_list
    : public std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> {
 public:
  string_list()
      : std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>{
            nullptr, curl_slist_free_all} {}

  void append(const char* header) {
    const auto new_slist = curl_slist_append(get(), header);
    if (!new_slist) throw std::runtime_error{"curl_list_append failed"};
    release();
    reset(new_slist);
  }

  void append(const std::string& str) { append(str.c_str()); }
};

template <typename... Args>
void setopt(CURL* curl, CURLoption option, Args&&... args) {
  const auto ret = curl_easy_setopt(curl, option, args...);
  if (ret != CURLE_OK)
    throw std::runtime_error{
        string::cat("curl_easy_setopt failed: ", curl_easy_strerror(ret))};
}

}  // namespace curl
