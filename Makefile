CC = cc
TARGET = dram_test

CFLAGS = -std=c11 -Wall -Wextra -Werror -O2
DRAM_MB = 64

OBJS = main.o dram_model.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): main.o dram_model.o
	$(CC) $(CFLAGS) main.o dram_model.o -o $(TARGET)

main.o: main.c dram_model.h
	$(CC) $(CFLAGS) -c main.c -o main.o

dram_model.o: dram_model.c dram_model.h
	$(CC) $(CFLAGS) -c dram_model.c -o dram_model.o

run: all
	./$(TARGET) $(DRAM_MB)

clean:
	rm -f $(TARGET) $(OBJS)