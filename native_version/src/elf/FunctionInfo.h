#ifndef ELFCFG_ELF_FUNCTIONINFO_H
#define ELFCFG_ELF_FUNCTIONINFO_H

#include <cstddef>
#include <cstdint>
#include <cstring>

struct FunctionInfo {
    char           name[128];
    uint64_t       virtualAddress;
    uint64_t       size;
    uint64_t       fileOffset;
    const unsigned char *code;
    int            sectionIndex;
    int            symbolIndex;
    unsigned char  bind;

    void clear(void)
    {
        memset(this, 0, sizeof(*this));
        sectionIndex = -1;
        symbolIndex  = -1;
    }

    bool hasCode(void) const
    {
        return code != NULL && size > 0;
    }

    uint64_t endAddress(void) const
    {
        return virtualAddress + size;
    }

    bool containsAddress(uint64_t address) const
    {
        return address >= virtualAddress && address < endAddress();
    }

    uint64_t offsetOf(uint64_t address) const
    {
        return (address >= virtualAddress) ? (address - virtualAddress) : 0;
    }
};

#endif
