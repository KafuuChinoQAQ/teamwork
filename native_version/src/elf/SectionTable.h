#ifndef ELFCFG_ELF_SECTIONTABLE_H
#define ELFCFG_ELF_SECTIONTABLE_H

#include <cstddef>
#include <cstdint>
#include <elf.h>

#include "elf/StringTable.h"

class ElfHeaderView;

class SectionTable {
public:
    SectionTable();

    bool parse(const unsigned char *fileData,
               size_t fileSize,
               const ElfHeaderView &header);

    bool           isValid(void) const { return parsed_; }
    unsigned short count(void) const   { return count_; }

    const Elf64_Shdr *at(unsigned short index) const;

    bool        bindNames(const unsigned char *data, size_t size);
    const char *nameOf(unsigned short index) const;
    int         findByName(const char *name) const;

    const unsigned char *dataOf(unsigned short index) const;
    uint64_t             sizeOf(unsigned short index) const;
    uint64_t             addressOf(unsigned short index) const;
    uint64_t             offsetOf(unsigned short index) const;

    int findByAddress(uint64_t address) const;

    int textIndex(void)   const { return idxText_;   }
    int symtabIndex(void) const { return idxSymtab_; }
    int strtabIndex(void) const { return idxStrtab_; }
    int dynsymIndex(void) const { return idxDynsym_; }
    int dynstrIndex(void) const { return idxDynstr_; }

    void dump(void) const;

private:
    const unsigned char *fileData_;
    size_t               fileSize_;
    const Elf64_Shdr    *headers_;
    unsigned short       count_;
    bool                 parsed_;

    StringTable          names_;

    int idxText_;
    int idxSymtab_;
    int idxStrtab_;
    int idxDynsym_;
    int idxDynstr_;

    void cacheWellKnown(void);

    SectionTable(const SectionTable &);
    SectionTable &operator=(const SectionTable &);
};

#endif
