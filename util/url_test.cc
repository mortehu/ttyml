#include "util/url.h"

#include "third_party/gtest/include/gtest/gtest.h"

namespace {

TEST(UrlTest, Parse) {
  EXPECT_EQ(url::parts("http:", "//www.example.org", "/abc", "#def"),
            url::parse("http://www.example.org/abc#def"));

  EXPECT_EQ(url::parts("http:", "//www.example.org", "/ABC", "#DEF"),
            url::parse("HTTP://WWW.EXAMPLE.ORG/ABC#DEF"));

  EXPECT_EQ(url::parse("http://www.example.org/"),
            url::parse("http://www.example.org"));

  EXPECT_EQ(url::parse("http://www.example.org/#abc"),
            url::parse("http://www.example.org#abc"));

  EXPECT_EQ(url::parts("", "//www.example.org", "/abc", "#def"),
            url::parse("//www.example.org/abc#def"));

  EXPECT_EQ(url::parts("", "//www.example.org", "/", "#def"),
            url::parse("//www.example.org#def"));

  EXPECT_EQ(url::parts("", "//www.example.org", "/abc", ""),
            url::parse("//www.example.org/abc"));

  EXPECT_EQ(url::parts("", "", "/abc", "#def"), url::parse("/abc#def"));

  EXPECT_EQ(url::parts("", "", "", "#def"), url::parse("#def"));

  EXPECT_EQ(url::parts("", "", "/abc", ""), url::parse("/abc"));
}

TEST(UrlTest, Base) {
  EXPECT_EQ("http://www.example.org/def#jkl",
            url::normalize("#jkl", "http://www.example.org/def#ghi"));

  EXPECT_EQ("http://www.example.org/abc",
            url::normalize("/abc", "http://www.example.org/def#ghi"));

  EXPECT_EQ("http://www.example.com/abc",
            url::normalize("//www.example.com/abc",
                           "http://www.example.org/def#ghi"));

  EXPECT_EQ("https://www.example.com/abc",
            url::normalize("https://www.example.com/abc",
                           "http://www.example.org/def#ghi"));
}

TEST(UrlTest, Relative) {
  EXPECT_EQ("http://www.example.org/def/",
            url::normalize("../", "http://www.example.org/def/ghi"));

  EXPECT_EQ("http://www.example.org/def/",
            url::normalize("../.", "http://www.example.org/def/ghi"));

  EXPECT_EQ("http://www.example.org/def/",
            url::normalize("..", "http://www.example.org/def/ghi"));

  EXPECT_EQ("http://www.example.org/",
            url::normalize("../../.", "http://www.example.org/def/ghi"));
}

}  // namespace
