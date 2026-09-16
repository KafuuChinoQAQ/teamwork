/* ============================================================================
 * elf/ElfHeaderView.h —— ELF 文件头视图
 * ----------------------------------------------------------------------------
 * "视图"的含义：本类不拷贝 ELF 数据，只是把映射区开头的字节
 * 按 Elf64_Ehdr 的结构来解释，并完成全部合法性校验。
 * 因此它的生命周期依赖于 BinaryFile 的映射区。
 *
 * 校验项（任何一项失败都会记录原因，parse() 返回 false）：
 *   1. 长度足够容纳 Elf64_Ehdr
 *   2. magic = 0x7F 'E' 'L' 'F'
 *   3. EI_CLASS   = ELFCLASS64
 *   4. EI_DATA    = ELFDATA2LSB（小端）
 *   5. EI_VERSION = EV_CURRENT
 *   6. e_machine  = EM_X86_64
 *   7. 节头表范围不越界
 * ========================================================================== */
#ifndef ELFCFG_ELF_ELFHEADERVIEW_H
#define ELFCFG_ELF_ELFHEADERVIEW_H

#include <cstddef>
#include <cstdint>
#include <elf.h>

class ElfHeaderView {
public:
    ElfHeaderView();

    /* 解析并校验映射区 [data, data + size) */
    bool parse(const unsigned char *data, size_t size);

    bool        isValid(void) const   { return valid_; }
    const char *errorText(void) const { return error_; }

    /* ---- 识别信息 ---- */
    unsigned char  elfClass(void) const;        /* ELFCLASS32 / ELFCLASS64 */
    unsigned char  dataEncoding(void) const;    /* ELFDATA2LSB / ELFDATA2MSB */
    unsigned char  abiVersion(void) const;
    unsigned short machine(void) const;         /* EM_X86_64 */
    unsigned short type(void) const;            /* ET_EXEC / ET_DYN / ET_REL */
    uint64_t       entry(void) const;
    uint64_t       programHeaderOffset(void) const;
    unsigned short programHeaderCount(void) const;
    unsigned short programHeaderEntrySize(void) const;
    uint64_t       sectionHeaderOffset(void) const;
    unsigned short sectionHeaderCount(void) const;
    unsigned short sectionHeaderEntrySize(void) const;
    unsigned short sectionNameIndex(void) const;   /* e_shstrndx */

    /* 原始结构体指针（指向映射区内部，只读） */
    const Elf64_Ehdr *raw(void) const { return ehdr_; }

    /* 人类可读的名字 */
    const char *machineName(void) const;
    const char *typeName(void) const;
    const char *className(void) const;
    const char *encodingName(void) const;

    /* 打印文件头摘要（供 --info 使用） */
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

#endif /* ELFCFG_ELF_ELFHEADERVIEW_H */
