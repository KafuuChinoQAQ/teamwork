/* ============================================================================
 * elf/SectionTable.cpp —— 节头表实现
 * ========================================================================== */
#include "elf/SectionTable.h"
#include "elf/ElfHeaderView.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

static const char *kTypeName[16] = {
    "NULL", "PROGBITS", "SYMTAB", "STRTAB", "RELA", "HASH", "DYNAMIC",
    "NOTE", "NOBITS", "REL", "SHLIB", "DYNSYM", "?", "?", "INIT_ARRAY",
    "FINI_ARRAY"
};

SectionTable::SectionTable()
    : fileData_(NULL), fileSize_(0), headers_(NULL), count_(0), parsed_(false),
      idxText_(-1), idxSymtab_(-1), idxStrtab_(-1), idxDynsym_(-1), idxDynstr_(-1)
{
}

bool SectionTable::parse(const unsigned char *fileData,
                         size_t fileSize,
                         const ElfHeaderView &header)
{
    fileData_ = fileData;
    fileSize_ = fileSize;
    parsed_   = false;
    headers_  = NULL;
    count_    = 0;

    if (!fileData || !header.isValid()) {
        logError("SectionTable: 输入无效（文件数据或 ELF 文件头未就绪）");
        return false;
    }

    uint64_t shoff = header.sectionHeaderOffset();
    unsigned short shnum = header.sectionHeaderCount();
    unsigned short shentsize = header.sectionHeaderEntrySize();

    if (shoff == 0 || shnum == 0) {
        logError("SectionTable: 该 ELF 没有节头表");
        return false;
    }
    if (shentsize < sizeof(Elf64_Shdr)) {
        logError("SectionTable: e_shentsize = %u 小于 Elf64_Shdr 大小 %lu",
                 (unsigned)shentsize, (unsigned long)sizeof(Elf64_Shdr));
        return false;
    }

    /* 范围已在 ElfHeaderView 中校验过，这里再确认一次（防御式编程） */
    uint64_t tableBytes = (uint64_t)shnum * (uint64_t)shentsize;
    if (shoff > (uint64_t)fileSize || tableBytes > (uint64_t)fileSize - shoff) {
        logError("SectionTable: 节头表超出文件范围");
        return false;
    }

    headers_ = (const Elf64_Shdr *)(fileData + shoff);
    count_   = shnum;
    parsed_  = true;

    /* 绑定节名字符串表 */
    unsigned short shstrndx = header.sectionNameIndex();
    if (shstrndx != SHN_UNDEF && shstrndx < count_) {
        const unsigned char *nm = dataOf(shstrndx);
        uint64_t             ns = sizeOf(shstrndx);
        if (nm && ns > 0) {
            names_.bind(nm, (size_t)ns);
        } else {
            logWarn("SectionTable: 节名字符串表（索引 %u）不可读", (unsigned)shstrndx);
        }
    } else {
        logWarn("SectionTable: e_shstrndx = %u 非法，节名不可用", (unsigned)shstrndx);
    }

    cacheWellKnown();

    logDebug("SectionTable: 共 %u 个节；.text=%d .symtab=%d .strtab=%d .dynsym=%d .dynstr=%d",
             (unsigned)count_, idxText_, idxSymtab_, idxStrtab_, idxDynsym_, idxDynstr_);
    return true;
}

void SectionTable::cacheWellKnown(void)
{
    idxText_   = findByName(".text");
    idxSymtab_ = findByName(".symtab");
    idxStrtab_ = findByName(".strtab");
    idxDynsym_ = findByName(".dynsym");
    idxDynstr_ = findByName(".dynstr");
}

const Elf64_Shdr *SectionTable::at(unsigned short index) const
{
    if (!parsed_ || index >= count_) return NULL;
    return &headers_[index];
}

bool SectionTable::bindNames(const unsigned char *data, size_t size)
{
    if (!data || size == 0) return false;
    names_.bind(data, size);
    return true;
}

const char *SectionTable::nameOf(unsigned short index) const
{
    const Elf64_Shdr *sh = at(index);
    if (!sh) return "<越界>";
    if (!names_.isValid()) return "<无节名表>";

    const char *s = names_.get(sh->sh_name);
    return s ? s : "<非法节名偏移>";
}

int SectionTable::findByName(const char *name) const
{
    if (!name || !parsed_) return -1;
    for (unsigned short i = 0; i < count_; ++i) {
        const Elf64_Shdr *sh = &headers_[i];
        if (!names_.isValid()) continue;
        const char *s = names_.get(sh->sh_name);
        if (s && strcmp(s, name) == 0) return (int)i;
    }
    return -1;
}

const unsigned char *SectionTable::dataOf(unsigned short index) const
{
    const Elf64_Shdr *sh = at(index);
    if (!sh) return NULL;

    /* .bss 之类的 NOBITS 节在文件中不占空间 */
    if (sh->sh_type == SHT_NOBITS) return NULL;
    if (sh->sh_size == 0) return NULL;

    if (sh->sh_offset > (uint64_t)fileSize_) return NULL;
    if (sh->sh_size > (uint64_t)fileSize_ - sh->sh_offset) return NULL;

    return fileData_ + sh->sh_offset;
}

uint64_t SectionTable::sizeOf(unsigned short index) const
{
    const Elf64_Shdr *sh = at(index);
    return sh ? (uint64_t)sh->sh_size : 0;
}

uint64_t SectionTable::addressOf(unsigned short index) const
{
    const Elf64_Shdr *sh = at(index);
    return sh ? (uint64_t)sh->sh_addr : 0;
}

uint64_t SectionTable::offsetOf(unsigned short index) const
{
    const Elf64_Shdr *sh = at(index);
    return sh ? (uint64_t)sh->sh_offset : 0;
}

int SectionTable::findByAddress(uint64_t address) const
{
    if (!parsed_) return -1;
    for (unsigned short i = 0; i < count_; ++i) {
        const Elf64_Shdr *sh = &headers_[i];
        /* 只考虑占据地址空间的节（有分配标志且大小非 0） */
        if (!(sh->sh_flags & SHF_ALLOC)) continue;
        if (sh->sh_size == 0) continue;
        if (address >= (uint64_t)sh->sh_addr &&
            address <  (uint64_t)sh->sh_addr + (uint64_t)sh->sh_size) {
            return (int)i;
        }
    }
    return -1;
}

void SectionTable::dump(void) const
{
    if (!parsed_) {
        fprintf(stderr, "节表：未解析\n");
        return;
    }

    fprintf(stderr, "节表（共 %u 项）:\n", (unsigned)count_);
    fprintf(stderr, "  %-4s %-20s %-14s %-12s %-12s %-8s %s\n",
            "Idx", "Name", "Type", "Address", "Offset", "Size", "Flags");

    for (unsigned short i = 0; i < count_; ++i) {
        const Elf64_Shdr *sh = &headers_[i];
        const char *typeName = (sh->sh_type < 16) ? kTypeName[sh->sh_type] : "?";

        char flags[8];
        int  f = 0;
        flags[f++] = (sh->sh_flags & SHF_ALLOC)     ? 'A' : '-';
        flags[f++] = (sh->sh_flags & SHF_EXECINSTR) ? 'X' : '-';
        flags[f++] = (sh->sh_flags & SHF_WRITE)     ? 'W' : '-';
        flags[f]   = '\0';

        fprintf(stderr, "  %-4u %-20s %-14s 0x%010llx 0x%08llx %-8llu %s\n",
                (unsigned)i,
                nameOf(i),
                typeName,
                (unsigned long long)sh->sh_addr,
                (unsigned long long)sh->sh_offset,
                (unsigned long long)sh->sh_size,
                flags);
    }
}
