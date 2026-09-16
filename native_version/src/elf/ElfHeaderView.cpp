/* ============================================================================
 * elf/ElfHeaderView.cpp —— ELF 文件头解析与校验
 * ========================================================================== */
#include "elf/ElfHeaderView.h"
#include "core/Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

ElfHeaderView::ElfHeaderView()
    : valid_(false), ehdr_(NULL), fileSize_(0)
{
    error_[0] = '\0';
}

void ElfHeaderView::setError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_, sizeof(error_), fmt, ap);
    va_end(ap);
    valid_ = false;
}

bool ElfHeaderView::parse(const unsigned char *data, size_t size)
{
    valid_    = false;
    ehdr_     = NULL;
    fileSize_ = size;
    error_[0] = '\0';

    /* ---- 1. 基本长度检查 ---- */
    if (!data) {
        setError("数据指针为空");
        return false;
    }
    if (size < sizeof(Elf64_Ehdr)) {
        setError("文件过小：%lu 字节，不足以容纳 ELF64 文件头（%lu 字节）",
                 (unsigned long)size, (unsigned long)sizeof(Elf64_Ehdr));
        return false;
    }

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)data;

    /* ---- 2. magic 校验 0x7F 'E' 'L' 'F' ---- */
    if (eh->e_ident[EI_MAG0] != ELFMAG0 ||
        eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 ||
        eh->e_ident[EI_MAG3] != ELFMAG3) {
        setError("不是 ELF 文件：magic 为 %02x %02x %02x %02x，应为 7f 45 4c 46",
                 eh->e_ident[EI_MAG0], eh->e_ident[EI_MAG1],
                 eh->e_ident[EI_MAG2], eh->e_ident[EI_MAG3]);
        return false;
    }

    /* ---- 3. 位宽：只支持 64 位 ---- */
    if (eh->e_ident[EI_CLASS] != ELFCLASS64) {
        setError("不是 ELF64（EI_CLASS = %u，本程序只支持 ELFCLASS64）",
                 (unsigned)eh->e_ident[EI_CLASS]);
        return false;
    }

    /* ---- 4. 字节序：只支持小端 ---- */
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB) {
        setError("不是小端 ELF（EI_DATA = %u，本程序只支持 ELFDATA2LSB）",
                 (unsigned)eh->e_ident[EI_DATA]);
        return false;
    }

    /* ---- 5. ELF 版本 ---- */
    if (eh->e_ident[EI_VERSION] != EV_CURRENT) {
        setError("不支持的 ELF 版本（EI_VERSION = %u）",
                 (unsigned)eh->e_ident[EI_VERSION]);
        return false;
    }

    /* ---- 6. 目标架构：只支持 x86-64 ---- */
    if (eh->e_machine != EM_X86_64) {
        setError("不是 x86-64 目标文件（e_machine = %u，本程序只支持 EM_X86_64 = %u）",
                 (unsigned)eh->e_machine, (unsigned)EM_X86_64);
        return false;
    }

    /* ---- 7. 节头表范围检查（防止后续按偏移访问时越界） ---- */
    if (eh->e_shoff != 0 && eh->e_shnum != 0) {
        if (eh->e_shentsize < sizeof(Elf64_Shdr)) {
            setError("节头表项过小：e_shentsize = %u，应至少为 %lu",
                     (unsigned)eh->e_shentsize, (unsigned long)sizeof(Elf64_Shdr));
            return false;
        }
        /* 用 64 位运算，避免 e_shoff + e_shnum * e_shentsize 溢出 */
        uint64_t tableBytes = (uint64_t)eh->e_shnum * (uint64_t)eh->e_shentsize;
        if (eh->e_shoff > (uint64_t)size ||
            tableBytes > (uint64_t)size - eh->e_shoff) {
            setError("节头表超出文件范围：偏移 %llu + 长度 %llu > 文件大小 %lu",
                     (unsigned long long)eh->e_shoff,
                     (unsigned long long)tableBytes,
                     (unsigned long)size);
            return false;
        }
    } else if (eh->e_shoff == 0) {
        logWarn("该 ELF 没有节头表（e_shoff = 0），无法按节查找符号");
    }

    /* ---- 8. 程序头表范围检查 ---- */
    if (eh->e_phoff != 0 && eh->e_phnum != 0 && eh->e_phentsize != 0) {
        uint64_t tableBytes = (uint64_t)eh->e_phnum * (uint64_t)eh->e_phentsize;
        if (eh->e_phoff > (uint64_t)size ||
            tableBytes > (uint64_t)size - eh->e_phoff) {
            setError("程序头表超出文件范围：偏移 %llu + 长度 %llu > 文件大小 %lu",
                     (unsigned long long)eh->e_phoff,
                     (unsigned long long)tableBytes,
                     (unsigned long)size);
            return false;
        }
    }

    ehdr_  = eh;
    valid_ = true;

    logDebug("ELF 文件头解析成功：class=%s endian=%s type=%s machine=%s entry=0x%llx",
             className(), encodingName(), typeName(), machineName(),
             (unsigned long long)eh->e_entry);
    return true;
}

/* ---- 访问器 ---- */
unsigned char ElfHeaderView::elfClass(void) const
{
    return ehdr_ ? ehdr_->e_ident[EI_CLASS] : 0;
}

unsigned char ElfHeaderView::dataEncoding(void) const
{
    return ehdr_ ? ehdr_->e_ident[EI_DATA] : 0;
}

unsigned char ElfHeaderView::abiVersion(void) const
{
    return ehdr_ ? ehdr_->e_ident[EI_ABIVERSION] : 0;
}

unsigned short ElfHeaderView::machine(void) const
{
    return ehdr_ ? ehdr_->e_machine : 0;
}

unsigned short ElfHeaderView::type(void) const
{
    return ehdr_ ? ehdr_->e_type : 0;
}

uint64_t ElfHeaderView::entry(void) const
{
    return ehdr_ ? (uint64_t)ehdr_->e_entry : 0;
}

uint64_t ElfHeaderView::programHeaderOffset(void) const
{
    return ehdr_ ? (uint64_t)ehdr_->e_phoff : 0;
}

unsigned short ElfHeaderView::programHeaderCount(void) const
{
    return ehdr_ ? ehdr_->e_phnum : 0;
}

unsigned short ElfHeaderView::programHeaderEntrySize(void) const
{
    return ehdr_ ? ehdr_->e_phentsize : 0;
}

uint64_t ElfHeaderView::sectionHeaderOffset(void) const
{
    return ehdr_ ? (uint64_t)ehdr_->e_shoff : 0;
}

unsigned short ElfHeaderView::sectionHeaderCount(void) const
{
    return ehdr_ ? ehdr_->e_shnum : 0;
}

unsigned short ElfHeaderView::sectionHeaderEntrySize(void) const
{
    return ehdr_ ? ehdr_->e_shentsize : 0;
}

unsigned short ElfHeaderView::sectionNameIndex(void) const
{
    return ehdr_ ? ehdr_->e_shstrndx : 0;
}

/* ---- 名字映射 ---- */
const char *ElfHeaderView::machineName(void) const
{
    if (!ehdr_) return "(未解析)";
    switch (ehdr_->e_machine) {
    case EM_X86_64: return "x86-64";
    case EM_386:    return "i386";
    case EM_ARM:    return "ARM";
    case EM_AARCH64:return "AArch64";
    default:        return "(其它架构)";
    }
}

const char *ElfHeaderView::typeName(void) const
{
    if (!ehdr_) return "(未解析)";
    switch (ehdr_->e_type) {
    case ET_NONE: return "ET_NONE";
    case ET_REL:  return "ET_REL(可重定位目标文件)";
    case ET_EXEC: return "ET_EXEC(可执行文件)";
    case ET_DYN:  return "ET_DYN(共享对象/PIE)";
    case ET_CORE: return "ET_CORE(core dump)";
    default:      return "(未知)";
    }
}

const char *ElfHeaderView::className(void) const
{
    if (!ehdr_) return "(未解析)";
    switch (ehdr_->e_ident[EI_CLASS]) {
    case ELFCLASS32: return "ELF32";
    case ELFCLASS64: return "ELF64";
    default:         return "(未知)";
    }
}

const char *ElfHeaderView::encodingName(void) const
{
    if (!ehdr_) return "(未解析)";
    switch (ehdr_->e_ident[EI_DATA]) {
    case ELFDATA2LSB: return "little-endian";
    case ELFDATA2MSB: return "big-endian";
    default:          return "(未知)";
    }
}

void ElfHeaderView::dump(void) const
{
    if (!valid_) {
        fprintf(stderr, "ELF 文件头：无效（%s）\n", error_);
        return;
    }
    fprintf(stderr, "ELF 文件头:\n");
    fprintf(stderr, "  类型      : %s\n", typeName());
    fprintf(stderr, "  架构      : %s (e_machine=%u)\n",
            machineName(), (unsigned)ehdr_->e_machine);
    fprintf(stderr, "  位宽/字节序: %s / %s\n", className(), encodingName());
    fprintf(stderr, "  入口地址  : 0x%llx\n", (unsigned long long)ehdr_->e_entry);
    fprintf(stderr, "  程序头    : 偏移 %llu，%u 项，每项 %u 字节\n",
            (unsigned long long)ehdr_->e_phoff,
            (unsigned)ehdr_->e_phnum, (unsigned)ehdr_->e_phentsize);
    fprintf(stderr, "  节头      : 偏移 %llu，%u 项，每项 %u 字节\n",
            (unsigned long long)ehdr_->e_shoff,
            (unsigned)ehdr_->e_shnum, (unsigned)ehdr_->e_shentsize);
    fprintf(stderr, "  节名表索引: %u\n", (unsigned)ehdr_->e_shstrndx);
}
