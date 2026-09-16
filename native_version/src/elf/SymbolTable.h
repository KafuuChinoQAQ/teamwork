/* ============================================================================
 * elf/SymbolTable.h —— 符号表视图（.symtab / .dynsym 通用）
 * ----------------------------------------------------------------------------
 * ELF 符号表本身只是一串 Elf64_Sym 数组，符号名需要到配套的字符串表
 * （.strtab 或 .dynstr）里按 st_name 偏移取。
 *
 * 本类同时持有这两块，提供：
 *   - 按名字查找函数符号（STT_FUNC）→ 填充 FunctionInfo
 *   - 遍历所有函数符号
 *   - 打印符号表
 *
 * 所有访问都做边界检查；符号表损坏时返回 false 而不是崩溃。
 * ========================================================================== */
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

    /* 绑定符号数组与配套字符串表。
     * sectionName 仅用于日志（如 ".symtab"）。
     * 返回 false 表示数据非法（长度不是 Elf64_Sym 的整数倍等）。 */
    bool parse(const unsigned char *symData, size_t symBytes,
               const unsigned char *strData, size_t strBytes,
               const char *sectionName);

    bool   isValid(void) const { return symbols_ != NULL; }
    size_t count(void)   const { return count_; }
    const char *sectionName(void) const { return sectionName_; }

    /* 取第 index 项；越界返回 NULL */
    const Elf64_Sym *at(size_t index) const;

    /* 取符号名；失败返回 NULL */
    const char *nameOf(const Elf64_Sym *sym) const;
    const char *nameOfIndex(size_t index) const;

    /* 符号类型/绑定（对 st_info 的解读） */
    static int typeOf(const Elf64_Sym *sym) { return ELF64_ST_TYPE(sym->st_info); }
    static int bindOf(const Elf64_Sym *sym) { return ELF64_ST_BIND(sym->st_info); }

    static const char *typeName(int type);
    static const char *bindName(int bind);

    /* 按名字查找函数符号并填充 FunctionInfo。
     * sections / fileData 用于把 st_value 换算成"文件偏移 + 机器码指针"。
     * 找到返回 true。 */
    bool findFunction(const char *name,
                      const SectionTable &sections,
                      const unsigned char *fileData,
                      size_t fileSize,
                      FunctionInfo *out) const;

    /* 用第 index 项符号填充 FunctionInfo（不检查类型） */
    bool fillFunctionInfo(size_t index,
                          const SectionTable &sections,
                          const unsigned char *fileData,
                          size_t fileSize,
                          FunctionInfo *out) const;

    /* 统计 STT_FUNC 符号个数 */
    size_t functionCount(void) const;

    /* 打印符号表（只打印 FUNC 与 OBJECT，最多 limit 条） */
    void dump(size_t limit) const;

private:
    const Elf64_Sym *symbols_;
    size_t           count_;
    StringTable      strings_;
    char             sectionName_[32];

    SymbolTable(const SymbolTable &);
    SymbolTable &operator=(const SymbolTable &);
};

#endif /* ELFCFG_ELF_SYMBOLTABLE_H */
