#include "dram_model.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_DRAM_MB 64UL
#define BYTES_PER_MB (1024UL * 1024UL)

static int parse_dram_size_mb(int argc, char **argv, size_t *out_mb)
{
    char *endptr = NULL;
    unsigned long value = 0;

    if (out_mb == NULL) {
        return -1;
    }

    if (argc < 2) {
        *out_mb = DEFAULT_DRAM_MB;
        return 0;
    }

    errno = 0;
    value = strtoul(argv[1], &endptr, 10);
    if (errno != 0 || endptr == argv[1] || *endptr != '\0' || value == 0) {
        return -1;
    }

    *out_mb = (size_t)value;
    return 0;
}

int main(int argc, char **argv)
{
    Dram dram = {0};
    size_t dram_mb = 0;
    size_t dram_bytes = 0;

    if (parse_dram_size_mb(argc, argv, &dram_mb) != 0) {
        fprintf(stderr, "Usage: %s [dram_size_mb]\n", argv[0]);
        return 1;
    }

    dram_bytes = dram_mb * BYTES_PER_MB;

    printf("[BOOT] C-Based DRAM Validation Simulator\n");
    printf("[DRAM] Initializing virtual DRAM: %zu MB\n", dram_mb);

    if (dram_init(&dram, dram_bytes) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize virtual DRAM\n");
        return 1;
    }

    printf("[DRAM] Virtual DRAM initialized: %zu bytes\n", dram_size_bytes(&dram));
    printf("[INFO] Day 1 commit 1 scope: allocation/free model only\n");
    printf("[INFO] Read/write APIs and memory tests will be added in later commits\n");

    dram_free(&dram);
    printf("[DRAM] Virtual DRAM released\n");

    return 0;
}
