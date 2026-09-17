/* ============================================================================
 * elf/FunctionTable.h —— 函数表
 * ----------------------------------------------------------------------------
 * 把符号表里的 STT_FUNC 条目统一收集成函数列表，并提供两种查找方式：
 *   - findByName()    ：按名字（课堂主用法）
 *   - findByAddress() ：按地址（后续分析 pass 会用到）
 *   - findContaining()：判断某地址落在哪个函数内
 *
 * 数据存储使用自制 SimpleVector，不使用 STL。
 * ========================================================================== */
#ifndef ELFCFG_ELF_FUNCTIONTABLE_H
#define ELFCFG_ELF_FUNCTIONTABLE_H

#include <cstddef>
#include <cstdint>

#include "core/SimpleVector.h"
#include "elf/FunctionInfo.h"

class SectionTable;
class SymbolTable;

class FunctionTable {
public:
    FunctionTable();

    /* 从符号表构建函数列表。sections/fileData/fileSize 用于换算机器码位置 */
    bool build(const SymbolTable &symtab,
               const SectionTable &sections,
               const unsigned char *fileData,
               size_t fileSize);

    size_t count(void) const { return functions_.size(); }
    bool   empty(void) const { return functions_.empty(); }

    const FunctionInfo *at(size_t index) const
    {
        return functions_.at(index);
    }

    /* 按名字精确查找；找不到返回 NULL */
    const FunctionInfo *findByName(const char *name) const;

    /* 按入口地址精确查找；找不到返回 NULL */
    const FunctionInfo *findByAddress(uint64_t address) const;

    /* 查找包含该地址的函数（地址落在函数范围内即可）；找不到返回 NULL */
    const FunctionInfo *findContaining(uint64_t address) const;

    /* 打印函数列表（供 --symbols 使用） */
    void dump(size_t limit) const;

    /* 统计信息 */
    uint64_t totalCodeBytes(void) const;

private:
    SimpleVector<FunctionInfo> functions_;

    FunctionTable(const FunctionTable &);
    FunctionTable &operator=(const FunctionTable &);
};

#endif /* ELFCFG_ELF_FUNCTIONTABLE_H */
