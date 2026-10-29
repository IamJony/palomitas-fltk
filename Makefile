# Makefile para Palomitas - Corregido para Tiny Core Linux
# Ubicación: Raíz del proyecto. Los fuentes están en src/

SRC      = src
CXX      = g++
CC       = gcc

# -fno-rtti es CRÍTICO en Tiny Core para evitar errores de typeinfo con FLTK
CXXFLAGS = $(shell fltk-config --cxxflags 2>/dev/null) -std=c++11 -O2 -Wall -fno-rtti
CFLAGS   = -O2 -Wall

# Enlazado: FLTK base + FLTK imágenes + matemáticas + hilos + libstdc++
LDFLAGS  = $(shell fltk-config --ldflags 2>/dev/null) -lfltk_images -lm -lpthread -lstdc++

OBJS   = $(SRC)/Palomitas-GUI.o $(SRC)/themes.o $(SRC)/library.o $(SRC)/cJSON.o
TARGET = palomitas

all: $(TARGET)

# Enlazado final (siempre con g++)
$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

# Compilación de archivos C++ (Usa CXXFLAGS que incluye -fno-rtti)
$(SRC)/Palomitas-GUI.o: $(SRC)/Palomitas-GUI.cpp $(SRC)/themes.h $(SRC)/library.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SRC)/themes.o: $(SRC)/themes.cpp $(SRC)/themes.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SRC)/library.o: $(SRC)/library.cpp $(SRC)/library.h $(SRC)/cJSON.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compilación de cJSON (Usa CFLAGS porque es C puro)
$(SRC)/cJSON.o: $(SRC)/cJSON.c $(SRC)/cJSON.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(SRC)/*.o $(TARGET)

.PHONY: all clean run
