CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Isrc -D_GLIBCXX_USE_CXX11_ABI=0
LIBS     = `fltk-config --ldflags` -lfltk_images

TARGET = palomitas-fltk
SRCS   = src/Palomitas-GUI.cpp src/themes.cpp src/library.cpp src/cJSON.c
OBJS   = $(SRCS:.cpp=.o)
OBJS   := $(OBJS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) `fltk-config --cxxflags` -o $@ $^ $(LIBS) -lstdc++

src/%.o: src/%.cpp src/themes.h src/library.h src/cJSON.h
	$(CXX) $(CXXFLAGS) `fltk-config --cxxflags` -c -o $@ $<

src/%.o: src/%.c
	$(CC) -O2 -Wall -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) src/*.o

.PHONY: all run clean
