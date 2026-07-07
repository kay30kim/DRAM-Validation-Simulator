CC = cc
TARGET = dram_test

CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -Icore
DRAM_MB = 64

OBJS = host/main.o core/dram_model.o core/memory_test.o host/logger.o core/error_injection.o host/plat_host.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

host/main.o: host/main.c core/dram_model.h core/memory_test.h host/logger.h core/error_injection.h
	$(CC) $(CFLAGS) -c host/main.c -o host/main.o

core/dram_model.o: core/dram_model.c core/dram_model.h core/plat.h
	$(CC) $(CFLAGS) -c core/dram_model.c -o core/dram_model.o

host/plat_host.o: host/plat_host.c core/plat.h
	$(CC) $(CFLAGS) -c host/plat_host.c -o host/plat_host.o

core/memory_test.o: core/memory_test.c core/memory_test.h core/dram_model.h
	$(CC) $(CFLAGS) -c core/memory_test.c -o core/memory_test.o

host/logger.o: host/logger.c host/logger.h core/memory_test.h
	$(CC) $(CFLAGS) -c host/logger.c -o host/logger.o

core/error_injection.o: core/error_injection.c core/error_injection.h core/dram_model.h
	$(CC) $(CFLAGS) -c core/error_injection.c -o core/error_injection.o

run: all
	./$(TARGET) $(DRAM_MB)

clean:
	rm -f $(OBJS) $(TARGET) dram_test_results.csv
