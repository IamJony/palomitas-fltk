# Makefile para Tiny Core Linux (i386)
CXX      = g++
CC       = gcc
CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=0 -fexceptions -frtti -Isrc
CFLAGS   = -O2 -Wall -Wextra -Isrc

# Flags de inclusión de FLTK (esto sí suele funcionar)
FLTK_CFLAGS := $(shell fltk-config --cxxflags 2>/dev/null)

# Flags de enlazado manuales (sin fltk-config --ldflags)
FLTK_LIBS   := -lfltk -lfltk_images -lX11 -lXext -lXft -lfontconfig -lpthread -ldl -lm

CXX_SRCS := src/Palomitas-GUI.cpp src/themes.cpp src/library.cpp
C_SRCS   := src/cJSON.c

CXX_OBJS := $(CXX_SRCS:src/%.cpp=build/%.o)
C_OBJS   := $(C_SRCS:src/%.c=build/%.o)
OBJS     := $(CXX_OBJS) $(C_OBJS)

TARGET   := palomitas-fltk

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(FLTK_LIBS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) $(FLTK_CFLAGS) -c $< -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

run: all
	./$(TARGET)

clean:
	rm -rf build $(TARGET)
