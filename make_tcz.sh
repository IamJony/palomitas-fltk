# Makefile
CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=0 -fno-rtti -fno-exceptions
CFLAGS   ?= -O2 -Wall -Wextra
LDFLAGS  ?=

FLTK_CFLAGS := $(shell fltk-config --cxxflags 2>/dev/null)
FLTK_LDFLAGS:= $(shell fltk-config --ldflags 2>/dev/null)

# Si fltk-config falla, usar flags manuales de Tiny Core
ifeq ($(strip $(FLTK_LDFLAGS)),)
  FLTK_LDFLAGS := -L/usr/local/lib -lfltk -ldl -lm -lXext -lX11
endif

CXXFLAGS += $(FLTK_CFLAGS) -Isrc
CFLAGS   += -Isrc

LDFLAGS  += $(FLTK_LDFLAGS) -lfltk_images -lstdc++ -lpthread -ldl -lm

CXX_SRCS := src/Palomitas-GUI.cpp src/themes.cpp src/library.cpp
C_SRCS   := src/cJSON.c

CXX_OBJS := $(CXX_SRCS:src/%.cpp=build/%.o)
C_OBJS   := $(C_SRCS:src/%.c=build/%.o)
OBJS     := $(CXX_OBJS) $(C_OBJS)

TARGET   := palomitas-fltk

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

run: all
	./$(TARGET)

clean:
	rm -rf build $(TARGET)
