#ifndef ELFCFG_ELF_SYMBOLTABLE_H
#define ELFCFG_ELF_SYMBOLTABLE_H

#include <cstddef>
#include <cstdint>
#include <elf.h>

#include "elf/StringTable.h"
#include "elf/FunctionInfo.h"

class SectionTable;

class SymbolTable {
public:
    SymbolTable();

    bool parse(const unsigned char *symData, size_t symBytes,
               const unsigned char *strData, size_t strBytes,
               const char *sectionName);

    bool   isValid(void) const { return symbols_ != NULL; }
    size_t count(void)   const { return count_; }
    const char *sectionName(void) const { return sectionName_; }

    const Elf64_Sym *at(size_t index) const;

    const char *nameOf(const Elf64_Sym *sym) const;
    const char *nameOfIndex(size_t index) const;

    static int typeOf(const Elf64_Sym *sym) { return ELF64_ST_TYPE(sym->st_info); }
    static int bindOf(const Elf64_Sym *sym) { return ELF64_ST_BIND(sym->st_info); }

    static const char *typeName(int type);
    static const char *bindName(int bind);

    bool findFunction(const char *name,
                      const SectionTable &sections,
                      const unsigned char *fileData,
                      size_t fileSize,
                      FunctionInfo *out) const;

    bool fillFunctionInfo(size_t index,
                          const SectionTable &sections,
                          const unsigned char *fileData,
                          size_t fileSize,
                          FunctionInfo *out) const;

    size_t functionCount(void) const;

    void dump(size_t limit) const;

private:
    const Elf64_Sym *symbols_;
    size_t           count_;
    StringTable      strings_;
    char             sectionName_[32];

    SymbolTable(const SymbolTable &);
    SymbolTable &operator=(const SymbolTable &);
};

#endif
