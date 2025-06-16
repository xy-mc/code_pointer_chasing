# Makefile for compiling ipv4 program for ARM64

# Compiler
CC = aarch64-linux-gnu-g++

# Compiler flags
CFLAGS = -Wall -ansi -std=c++11 -pedantic -g -O2 -pthread -static

# Include and library paths
INCLUDES = -I/home/luoqiang/xymc/gem5_dda/include
LIBS = -L/home/luoqiang/xymc/gem5_dda/util/m5/build/arm64/out -lm5 -lpthread -lm

# Source files
SRC = dynamic_sort.cpp

# Output executable
TARGET = dynamic_sort

# Default rule
all: $(TARGET)

# Linking
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(INCLUDES) $(LIBS)

# Clean rule
clean:
	rm -f $(TARGET)

.PHONY: all clean

