/* ============================================================================
 * elf/FunctionTable.cpp —— 函数表实现
 * ========================================================================== */
#include "elf/FunctionTable.h"
#include "elf/SymbolTable.h"
#include "elf/SectionTable.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

FunctionTable::FunctionTable()
{
}

bool FunctionTable::build(const SymbolTable &symtab,
                          const SectionTable &sections,
                          const unsigned char *fileData,
                          size_t fileSize)
{
    functions_.clear();

    if (!symtab.isValid()) {
        logWarn("FunctionTable: 符号表不可用，无法构建函数表");
        return false;
    }

    size_t total = symtab.count();
    for (size_t i = 0; i < total; ++i) {
        const Elf64_Sym *sym = symtab.at(i);
        if (!sym) continue;
        if (SymbolTable::typeOf(sym) != STT_FUNC) continue;

        FunctionInfo info;
        if (!symtab.fillFunctionInfo(i, sections, fileData, fileSize, &info)) {
            continue;
        }
        /* 只保留有实际机器码、且有名字的函数 */
        if (!info.hasCode()) continue;
        if (info.name[0] == '\0') continue;

        if (!functions_.pushBack(info)) {
            logError("FunctionTable: 追加函数 %s 失败（内存不足）", info.name);
            return false;
        }
    }

    logInfo("函数表    : 共 %lu 个函数（来自 %s）",
            (unsigned long)functions_.size(), symtab.sectionName());
    return true;
}

const FunctionInfo *FunctionTable::findByName(const char *name) const
{
    if (!name) return NULL;
    for (size_t i = 0; i < functions_.size(); ++i) {
        const FunctionInfo *f = functions_.at(i);
        if (f && strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

const FunctionInfo *FunctionTable::findByAddress(uint64_t address) const
{
    for (size_t i = 0; i < functions_.size(); ++i) {
        const FunctionInfo *f = functions_.at(i);
        if (f && f->virtualAddress == address) return f;
    }
    return NULL;
}

const FunctionInfo *FunctionTable::findContaining(uint64_t address) const
{
    for (size_t i = 0; i < functions_.size(); ++i) {
        const FunctionInfo *f = functions_.at(i);
        if (f && f->containsAddress(address)) return f;
    }
    return NULL;
}

uint64_t FunctionTable::totalCodeBytes(void) const
{
    uint64_t sum = 0;
    for (size_t i = 0; i < functions_.size(); ++i) {
        const FunctionInfo *f = functions_.at(i);
        if (f) sum += f->size;
    }
    return sum;
}

void FunctionTable::dump(size_t limit) const
{
    fprintf(stderr, "函数表（共 %lu 个函数）:\n",
            (unsigned long)functions_.size());
    fprintf(stderr, "  %-4s %-18s %-10s %-12s %s\n",
            "Idx", "Address", "Size", "FileOffset", "Name");

    size_t shown = 0;
    for (size_t i = 0; i < functions_.size() && shown < limit; ++i) {
        const FunctionInfo *f = functions_.at(i);
        if (!f) continue;
        fprintf(stderr, "  %-4lu 0x%016llx %-10llu 0x%08llx   %s\n",
                (unsigned long)i,
                (unsigned long long)f->virtualAddress,
                (unsigned long long)f->size,
                (unsigned long long)f->fileOffset,
                f->name);
        shown++;
    }
    if (functions_.size() > shown) {
        fprintf(stderr, "  ...（其余 %lu 个函数已省略）\n",
                (unsigned long)(functions_.size() - shown));
    }
}
