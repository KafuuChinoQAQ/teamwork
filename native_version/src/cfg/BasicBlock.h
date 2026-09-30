#ifndef ELFCFG_CFG_BASICBLOCK_H
#define ELFCFG_CFG_BASICBLOCK_H

#include <cstddef>
#include <cstdint>

#include "arch/Instruction.h"

struct BasicBlock {
    int          id;
    uint64_t     startAddress;
    uint64_t     endAddress;
    Instruction *first;
    Instruction *last;

    bool         reachable;
    bool         loopHeader;

    BasicBlock  *next;

    int instructionCount(void) const
    {
        int n = 0;
        for (const Instruction *ins = first; ins != NULL; ins = ins->next) {
            n++;
            if (ins == last) break;
        }
        return n;
    }

    bool contains(uint64_t address) const
    {
        return address >= startAddress && address < endAddress;
    }

    uint64_t size(void) const { return endAddress - startAddress; }
};

#endif
