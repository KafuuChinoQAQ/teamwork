/* ============================================================================
 * elf/StringTable.h —— ELF 字符串表视图
 * ----------------------------------------------------------------------------
 * ELF 中的 .strtab / .dynstr / .shstrtab 都是同一格式：
 *   一串以 '\0' 结尾的字节序列，通过"偏移量"引用其中的字符串。
 *
 * 本类只保存"起始指针 + 长度"，所有取字符串的操作都做边界检查：
 *   - 偏移必须落在表内；
 *   - 必须能在表内找到结尾 '\0'（否则返回 NULL，不产生越界读）。
 *
 * 不使用 std::string：返回的 const char* 直接指向映射区内部，零拷贝。
 * ========================================================================== */
#ifndef ELFCFG_ELF_STRINGTABLE_H
#define ELFCFG_ELF_STRINGTABLE_H

#include <cstddef>
#include <cstdint>

class StringTable {
public:
    StringTable();

    /* 绑定到一块内存（通常是映射区中某个节的 [sh_offset, sh_offset+sh_size)） */
    void bind(const unsigned char *base, size_t size);

    void unbind(void);

    bool   isValid(void) const { return base_ != NULL && size_ > 0; }
    size_t size(void) const    { return size_; }

    /* 按偏移取字符串。
     * 成功返回指向表内的 const char*；偏移越界或缺少结尾 '\0' 时返回 NULL */
    const char *get(uint32_t offset) const;

    /* 安全版本：取不到时返回 fallback（默认 "<非法偏移>"），便于打印 */
    const char *getOr(uint32_t offset, const char *fallback) const;

    /* 遍历：返回第 index 个字符串的偏移；越界返回 -1 */
    long entryOffset(size_t index) const;

    /* 统计表内字符串个数（以 '\0' 分隔计数） */
    size_t entryCount(void) const;

    /* 打印整张表（供调试） */
    void dump(const char *title) const;

private:
    const unsigned char *base_;
    size_t               size_;

    StringTable(const StringTable &);
    StringTable &operator=(const StringTable &);
};

#endif /* ELFCFG_ELF_STRINGTABLE_H */
