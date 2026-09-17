/* ============================================================================
 * elf/SectionTable.h —— 节头表
 * ----------------------------------------------------------------------------
 * 负责：
 *   1. 把映射区中的节头表解释为 Elf64_Shdr 数组；
 *   2. 绑定节名字符串表 (.shstrtab)，提供按名字查找；
 *   3. 提供节的"文件偏移 / 大小 / 虚拟地址"访问，并做越界检查；
 *   4. 缓存常用节的索引：.text / .symtab / .strtab / .dynsym / .dynstr。
 *
 * 所有对外接口都保证：越界时返回 NULL / -1，而不是产生非法指针。
 * ========================================================================== */
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

    /* 依据文件头定位并解析节头表 */
    bool parse(const unsigned char *fileData,
               size_t fileSize,
               const ElfHeaderView &header);

    bool           isValid(void) const { return parsed_; }
    unsigned short count(void) const   { return count_; }

    /* 取第 index 项的节头结构；越界返回 NULL */
    const Elf64_Shdr *at(unsigned short index) const;

    /* ---- 节名 ---- */
    bool        bindNames(const unsigned char *data, size_t size);
    const char *nameOf(unsigned short index) const;      /* 越界返回 "<非法>" */
    int         findByName(const char *name) const;      /* 找不到返回 -1 */

    /* ---- 节数据（全部带边界检查） ---- */
    const unsigned char *dataOf(unsigned short index) const;   /* 越界返回 NULL */
    uint64_t             sizeOf(unsigned short index) const;
    uint64_t             addressOf(unsigned short index) const;
    uint64_t             offsetOf(unsigned short index) const;

    /* 根据虚拟地址找所属节；找不到返回 -1 */
    int findByAddress(uint64_t address) const;

    /* ---- 常用节索引（未找到返回 -1） ---- */
    int textIndex(void)   const { return idxText_;   }
    int symtabIndex(void) const { return idxSymtab_; }
    int strtabIndex(void) const { return idxStrtab_; }
    int dynsymIndex(void) const { return idxDynsym_; }
    int dynstrIndex(void) const { return idxDynstr_; }

    /* 打印节表（供 --sections 使用） */
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

    /* 解析后缓存常用节索引 */
    void cacheWellKnown(void);

    SectionTable(const SectionTable &);
    SectionTable &operator=(const SectionTable &);
};

#endif /* ELFCFG_ELF_SECTIONTABLE_H */
