#ifndef ADDR_MAP_H
#define ADDR_MAP_H

#include <stdint.h>

typedef struct MappedLocation
{
    uint32_t cs;
    uint32_t cid;
    uint32_t bg;
    uint32_t ba;
    uint32_t row;
    uint32_t column;
} MappedLocation;

// 128GB 2Hi는 CID 1비트, 256GB 4Hi는 CID 2비트를 사용
typedef struct AddressProfile
{
    const char *name;
    const char *desc;

    uint32_t cid_bits;
    uint32_t logical_ranks;
    uint32_t page_size_bytes;
} AddressProfile;

typedef int (*AddressMapFn)(uint64_t packed,
                            const AddressProfile *profile,
                            MappedLocation *out);

typedef struct AddressMap
{
    const char *name;
    const char *desc;
    AddressMapFn decode;
} AddressMap;

const AddressProfile *addr_profile_by_name(const char *name);
const AddressMap *addr_map_by_name(const char *name);

int addr_decode(const AddressProfile *profile,
                const AddressMap *map,
                uint64_t packed,
                MappedLocation *out);

#endif
