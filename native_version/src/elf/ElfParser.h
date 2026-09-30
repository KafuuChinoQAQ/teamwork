#ifndef ELFCFG_ELF_ELFPARSER_H
#define ELFCFG_ELF_ELFPARSER_H

#include <cstddef>
#include <cstdint>

#include "elf/ElfHeaderView.h"
#include "elf/SectionTable.h"
#include "elf/StringTable.h"
#include "elf/SymbolTable.h"
#include "elf/FunctionTable.h"

class BinaryFile;

class ElfParser {
public:
    ElfParser();
    ~ElfParser();

    bool open(const char *path);
    void close(void);
    bool isOpen(void) const { return opened_; }
    const char *lastError(void) const { return error_; }

    BinaryFile          *file(void)     const { return file_;     }
    const ElfHeaderView &header(void)   const { return header_;   }
    const SectionTable  &sections(void) const { return sections_; }
    const SymbolTable   &symtab(void)   const { return symtab_;   }
    const SymbolTable   &dynsym(void)   const { return dynsym_;   }
    const FunctionTable &functions(void)const { return functions_;}

    const unsigned char *imageData(void) const;
    size_t               imageSize(void) const;

    bool findFunction(const char *name, FunctionInfo *out) const;

    void dumpInfo(void) const;
    void dumpSections(void) const;
    void dumpSymbols(void) const;
    void dumpFunctions(void) const;
    void dumpSectionHex(void) const;

private:
    BinaryFile   *file_;
    ElfHeaderView header_;
    SectionTable  sections_;
    SymbolTable   symtab_;
    SymbolTable   dynsym_;
    FunctionTable functions_;

    bool  opened_;
    char  error_[256];

    bool parseSections(void);
    bool parseSymbolTables(void);
    void setError(const char *fmt, ...);

    ElfParser(const ElfParser &);
    ElfParser &operator=(const ElfParser &);
};

#endif
