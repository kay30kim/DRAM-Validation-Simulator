#include "addr_map.h"

#include <string.h>

// 테스트용 기본 주소 배치
// COL[9:0] | BA[11:10] | BG[14:12] | ROW[26:15] | CID[27] | CS[28]
static void map_linear(uint32_t phys, MappedLocation *out)
{
    out->column = phys & 0x3FFU;
    out->bank = (phys >> 10) & 0x3U;
    out->bg = (phys >> 12) & 0x7U;
    out->row = (phys >> 15) & 0xFFFU;
    out->cid = (phys >> 27) & 0x1U;
    out->cs = (phys >> 28) & 0x1U;
}

// Row 비트 일부를 BG와 BA 선택에 섞어 접근을 여러 bank로 분산
static void map_bank_hash(uint32_t phys, MappedLocation *out)
{
    map_linear(phys, out);

    out->bg ^= out->row & 0x7U;
    out->bank ^= (out->row >> 3) & 0x3U;
}

// 낮은 주소 비트 하나를 CS 선택에 사용하는 단순한 rank interleave 예시
static void map_rank_interleave(uint32_t phys, MappedLocation *out)
{
    map_linear(phys, out);

    out->cs = (phys >> 13) & 0x1U;
}

static const AddressMap kMaps[] = {
    { "linear", "direct bit slicing", map_linear },
    { "bank_hash", "row-based bank hash", map_bank_hash },
    { "rank_interleave", "low-bit rank interleave", map_rank_interleave },
};

const AddressMap *addr_map_by_name(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(kMaps) / sizeof(kMaps[0]); i++)
    {
        if (strcmp(name, kMaps[i].name) == 0)
        {
            return &kMaps[i];
        }
    }

    return NULL;
}