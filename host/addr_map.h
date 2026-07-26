#ifndef ADDR_MAP_H
#define ADDR_MAP_H

#include <stdint.h>

typedef struct MappedLocation
{
    uint32_t cs;
    uint32_t cid;
    uint32_t bg;
    uint32_t bank;
    uint32_t row;
    uint32_t column;
} MappedLocation;

typedef void (*AddressMapFn)(uint32_t phys, MappedLocation *out);

typedef struct AddressMap
{
    const char *name;
    const char *desc;
    AddressMapFn decode;
} AddressMap;

const AddressMap *addr_map_by_name(const char *name);

#endif