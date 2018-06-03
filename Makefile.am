AM_CXXFLAGS = -std=c++14 -Wall
AM_CPPFLAGS = -I. $(CURL_CFLAGS) $(EXPAT_CFLAGS)
AM_LDFLAGS = -pthread

bin_PROGRAMS = ttyml
check_PROGRAMS = \
  util/path_test \
  util/url_test
noinst_LIBRARIES =

TESTS = $(check_PROGRAMS)

ttyml_SOURCES = main.cc ttyml.cc ttyml.h
ttyml_LDADD = $(CURL_LIBS) $(EXPAT_LIBS) -lreadline

util_path_test_SOURCES = util/path_test.cc
util_path_test_LDADD = third_party/gtest/libgtest.a

util_url_test_SOURCES = util/url_test.cc
util_url_test_LDADD = third_party/gtest/libgtest.a

include $(srcdir)/third_party/gtest/Makefile.am
