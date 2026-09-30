#ifndef ELFCFG_ELF_ELFHEADERVIEW_H
#define ELFCFG_ELF_ELFHEADERVIEW_H

#include <cstddef>
#include <cstdint>
#include <elf.h>

class ElfHeaderView {
public:
    ElfHeaderView();

    bool parse(const unsigned char *data, size_t size);

    bool        isValid(void) const   { return valid_; }
    const char *errorText(void) const { return error_; }

    unsigned char  elfClass(void) const;
    unsigned char  dataEncoding(void) const;
    unsigned char  abiVersion(void) const;
    unsigned short machine(void) const;
    unsigned short type(void) const;
    uint64_t       entry(void) const;
    uint64_t       programHeaderOffset(void) const;
    unsigned short programHeaderCount(void) const;
    unsigned short programHeaderEntrySize(void) const;
    uint64_t       sectionHeaderOffset(void) const;
    unsigned short sectionHeaderCount(void) const;
    unsigned short sectionHeaderEntrySize(void) const;
    unsigned short sectionNameIndex(void) const;

    const Elf64_Ehdr *raw(void) const { return ehdr_; }

    const char *machineName(void) const;
    const char *typeName(void) const;
    const char *className(void) const;
    const char *encodingName(void) const;

    void dump(void) const;

private:
    bool              valid_;
    const Elf64_Ehdr *ehdr_;
    size_t            fileSize_;
    char              error_[256];

    void setError(const char *fmt, ...);

    ElfHeaderView(const ElfHeaderView &);
    ElfHeaderView &operator=(const ElfHeaderView &);
};

#endif
