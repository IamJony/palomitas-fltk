# Makefile para Tiny Core Linux (i386)
CXX      = g++
CC       = gcc
CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=0 -fno-rtti -fno-exceptions -Isrc
CFLAGS   = -O2 -Wall -Wextra -Isrc

# Incluimos los flags de FLTK (pero forzamos el orden de las librerías manualmente)
FLTK_CFLAGS := $(shell fltk-config --cxxflags 2>/dev/null)
CXXFLAGS += $(FLTK_CFLAGS)

# Enlazado manual y forzado para Tiny Core (evita que el linker falle por typeinfo)
# -Wl,--unresolved-symbols=ignore-in-shared-libs le dice al linker que ignore
# los typeinfo que FLTK no exporta por estar compilada sin RTTI.
LDFLAGS = -L/usr/local/lib -lfltk -lfltk_images -lX11 -lXext -lXft -lfontconfig -lpthread -ldl -lm -lstdc++ -Wl,--unresolved-symbols=ignore-in-shared-libs

CXX_SRCS = src/Palomitas-GUI.cpp src/themes.cpp src/library.cpp
C_SRCS   = src/cJSON.c

CXX_OBJS = $(CXX_SRCS:src/%.cpp=build/%.o)
C_OBJS   = $(C_SRCS:src/%.c=build/%.o)
OBJS     = $(CXX_OBJS) $(C_OBJS)

TARGET   = palomitas-fltk

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
