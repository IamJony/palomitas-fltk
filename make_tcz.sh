# Makefile
CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=0
CFLAGS   ?= -O2 -Wall -Wextra
LDFLAGS  ?=

FLTK_CFLAGS := $(shell fltk-config --cxxflags)
FLTK_LDFLAGS:= $(shell fltk-config --ldflags)

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null || echo "-I/usr/include")
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null || echo "-lcurl")

CXXFLAGS += $(FLTK_CFLAGS) $(CURL_CFLAGS) -Isrc
CFLAGS   += $(CURL_CFLAGS) -Isrc
LDFLAGS  += $(FLTK_LDFLAGS) -lfltk -lfltk_images $(CURL_LIBS) -lstdc++ -lpthread -ldl -lm

CXX_SRCS := src/Palomitas-GUI.cpp src/themes.cpp src/library.cpp
C_SRCS   := src/cJSON.c

CXX_OBJS := $(CXX_SRCS:src/%.cpp=build/%.o)
C_OBJS   := $(C_SRCS:src/%.c=build/%.o)
OBJS     := $(CXX_OBJS) $(C_OBJS)

TARGET   := palomitas-fltk

.PHONY: all clean install run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

install: all
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/palomitas-fltk

run: all
	./$(TARGET)

clean:
	rm -rf build $(TARGET)
