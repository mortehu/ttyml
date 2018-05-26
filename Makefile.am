AM_CXXFLAGS = -std=c++14 -Wall
AM_CPPFLAGS = -I. $(CURL_CFLAGS) $(EXPAT_CFLAGS)

bin_PROGRAMS = ttyml

ttyml_SOURCES = main.cc ttyml.cc ttyml.h
ttyml_LDADD = $(CURL_LIBS) $(EXPAT_LIBS) -lreadline
