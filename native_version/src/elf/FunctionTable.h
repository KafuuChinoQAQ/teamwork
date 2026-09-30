#ifndef ELFCFG_ELF_FUNCTIONTABLE_H
#define ELFCFG_ELF_FUNCTIONTABLE_H

#include <cstddef>
#include <cstdint>

#include "core/SimpleVector.h"
#include "elf/FunctionInfo.h"

class SectionTable;
class SymbolTable;

class FunctionTable {
public:
    FunctionTable();

    bool build(const SymbolTable &symtab,
               const SectionTable &sections,
               const unsigned char *fileData,
               size_t fileSize);

    size_t count(void) const { return functions_.size(); }
    bool   empty(void) const { return functions_.empty(); }

    const FunctionInfo *at(size_t index) const
    {
        const SimpleVector<FunctionInfo> &rows = functions_;
        return rows.at(index);
    }

    const FunctionInfo *findByName(const char *name) const;

    const FunctionInfo *findByAddress(uint64_t address) const;

    const FunctionInfo *findContaining(uint64_t address) const;

    void dump(size_t limit) const;

    uint64_t totalCodeBytes(void) const;

private:
    SimpleVector<FunctionInfo> functions_;

    FunctionTable(const FunctionTable &);
    FunctionTable &operator=(const FunctionTable &);
};

#endif
