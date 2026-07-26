#include "addr_map.h"

#include <stddef.h>
#include <string.h>

/*
 * 테스트 내부에서 사용하는 packed address 형식
 * COLUMN : bits [9:0]   - C0~C9
 * BA     : bits [11:10] - BA0~BA1
 * BG     : bits [14:12] - BG0~BG2
 * ROW    : bits [30:15] - R0~R15
 * CID    : bit 31부터 profile에 따라 1~2비트
 * CS     : CID 다음 비트
 */

#define COLUMN_SHIFT 0U
#define BA_SHIFT     10U
#define BG_SHIFT     12U
#define ROW_SHIFT    15U
#define CID_SHIFT    31U

#define COLUMN_MASK UINT64_C(0x3FF)
#define BA_MASK     UINT64_C(0x3)
#define BG_MASK     UINT64_C(0x7)
#define ROW_MASK    UINT64_C(0xFFFF)
#define CS_MASK     UINT64_C(0x1)

static const AddressProfile kProfiles[] = {
    {
        "128gb_2hi",
        "128GB 4Rx4, TSV 2Hi",
        1U,
        4U,
        1024U
    },
    {
        "256gb_4hi",
        "256GB 8Rx4, TSV 4Hi",
        2U,
        8U,
        1024U
    },
};

static uint64_t mask_for_bits(uint32_t bits)
{
    return (UINT64_C(1) << bits) - UINT64_C(1);
}

static int profile_is_valid(const AddressProfile *profile)
{
    if (profile == NULL)
    {
        return 0;
    }

    return profile->cid_bits == 1U ||
           profile->cid_bits == 2U;
}

/*
 * BG, BA, ROW, COLUMN, CID, CS를 순서대로 꺼낸다.
 */
static int map_linear(uint64_t packed,
                      const AddressProfile *profile,
                      MappedLocation *out)
{
    uint32_t cs_shift;
    uint32_t used_bits;
    uint64_t cid_mask;

    if (!profile_is_valid(profile) || out == NULL)
    {
        return -1;
    }

    cid_mask = mask_for_bits(profile->cid_bits);
    cs_shift = CID_SHIFT + profile->cid_bits;
    used_bits = cs_shift + 1U;

    if ((packed >> used_bits) != 0U)
    {
        return -1;
    }

    out->column =
        (uint32_t)((packed >> COLUMN_SHIFT) & COLUMN_MASK);

    out->ba =
        (uint32_t)((packed >> BA_SHIFT) & BA_MASK);

    out->bg =
        (uint32_t)((packed >> BG_SHIFT) & BG_MASK);

    out->row =
        (uint32_t)((packed >> ROW_SHIFT) & ROW_MASK);

    out->cid =
        (uint32_t)((packed >> CID_SHIFT) & cid_mask);

    out->cs =
        (uint32_t)((packed >> cs_shift) & CS_MASK);

    return 0;
}

// Row 비트 일부를 BG와 BA 선택에 섞는다. (Address Hashing)
static int map_bank_hash(uint64_t packed,
                         const AddressProfile *profile,
                         MappedLocation *out)
{
    if (map_linear(packed, profile, out) != 0)
    {
        return -1;
    }

    out->bg ^= out->row & 0x7U;
    out->ba ^= (out->row >> 3) & 0x3U;

    return 0;
}

// Row의 하위 비트를 CS 선택에 섞는다. (Address Interleaving)
static int map_rank_interleave(uint64_t packed,
                               const AddressProfile *profile,
                               MappedLocation *out)
{
    if (map_linear(packed, profile, out) != 0)
    {
        return -1;
    }

    out->cs ^= out->row & 0x1U;

    return 0;
}

static const AddressMap kMaps[] = {
    {
        "linear",
        "direct bit slicing",
        map_linear
    },
    {
        "bank_hash",
        "row-based bank hash",
        map_bank_hash
    },
    {
        "rank_interleave",
        "row-based rank interleave",
        map_rank_interleave
    },
};

const AddressProfile *addr_profile_by_name(const char *name)
{
    size_t i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0; i < sizeof(kProfiles) / sizeof(kProfiles[0]); i++)
    {
        if (strcmp(name, kProfiles[i].name) == 0)
        {
            return &kProfiles[i];
        }
    }

    return NULL;
}

const AddressMap *addr_map_by_name(const char *name)
{
    size_t i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0; i < sizeof(kMaps) / sizeof(kMaps[0]); i++)
    {
        if (strcmp(name, kMaps[i].name) == 0)
        {
            return &kMaps[i];
        }
    }

    return NULL;
}

int addr_decode(const AddressProfile *profile,
                const AddressMap *map,
                uint64_t packed,
                MappedLocation *out)
{
    if (profile == NULL ||
        map == NULL ||
        map->decode == NULL ||
        out == NULL)
    {
        return -1;
    }

    return map->decode(packed, profile, out);
}
