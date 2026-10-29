# Makefile reparado - Asegura compilación C++ con g++ y enlazado con libstdc++
SRC      = src
CXX      = g++
CC       = gcc

# Flags de FLTK y C++11
CXXFLAGS = $(shell fltk-config --cxxflags 2>/dev/null) -std=c++11 -O2 -Wall
CFLAGS   = -O2 -Wall

# Enlazado: FLTK + imágenes + matemáticas + hilos + libstdc++ explícita
LDFLAGS  = $(shell fltk-config --ldflags 2>/dev/null) -lfltk_images -lm -lpthread -lstdc++

OBJS   = $(SRC)/Palomitas-GUI.o $(SRC)/themes.o $(SRC)/library.o $(SRC)/cJSON.o
TARGET = palomitas

all: $(TARGET)

# El enlazado final SIEMPRE se hace con g++
$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

# Archivos C++ (usar g++)
$(SRC)/Palomitas-GUI.o: $(SRC)/Palomitas-GUI.cpp $(SRC)/themes.h $(SRC)/library.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SRC)/themes.o: $(SRC)/themes.cpp $(SRC)/themes.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SRC)/library.o: $(SRC)/library.cpp $(SRC)/library.h $(SRC)/cJSON.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Archivo C (usar gcc)
$(SRC)/cJSON.o: $(SRC)/cJSON.c $(SRC)/cJSON.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(SRC)/*.o $(TARGET)

.PHONY: all clean run
