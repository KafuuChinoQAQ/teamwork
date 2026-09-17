/* ============================================================================
 * elf/FunctionInfo.h —— 函数描述
 * ----------------------------------------------------------------------------
 * 由符号表条目（Elf64_Sym, STT_FUNC）解析而来，是 ELF 层交给解码层的
 * 唯一"接口对象"：告诉它"函数叫什么、在哪、有多长、机器码从哪开始"。
 * ========================================================================== */
#ifndef ELFCFG_ELF_FUNCTIONINFO_H
#define ELFCFG_ELF_FUNCTIONINFO_H

#include <cstddef>
#include <cstdint>
#include <cstring>

struct FunctionInfo {
    char           name[128];        /* 函数名（截断保护）            */
    uint64_t       virtualAddress;   /* st_value：函数入口虚拟地址     */
    uint64_t       size;             /* st_size ：函数字节长度         */
    uint64_t       fileOffset;       /* 机器码在文件中的偏移           */
    const unsigned char *code;       /* 指向映射区内机器码首字节       */
    int            sectionIndex;     /* 所属节索引（-1 表示未知）      */
    int            symbolIndex;      /* 符号表下标（用于调试）         */
    unsigned char  bind;             /* STB_LOCAL / STB_GLOBAL ...    */

    void clear(void)
    {
        memset(this, 0, sizeof(*this));
        sectionIndex = -1;
        symbolIndex  = -1;
    }

    /* 是否有可解码的机器码 */
    bool hasCode(void) const
    {
        return code != NULL && size > 0;
    }

    /* 函数地址区间 [start, end) */
    uint64_t endAddress(void) const
    {
        return virtualAddress + size;
    }

    /* 判断某地址是否落在本函数内 */
    bool containsAddress(uint64_t address) const
    {
        return address >= virtualAddress && address < endAddress();
    }

    /* 相对函数入口的偏移 */
    uint64_t offsetOf(uint64_t address) const
    {
        return (address >= virtualAddress) ? (address - virtualAddress) : 0;
    }
};

#endif /* ELFCFG_ELF_FUNCTIONINFO_H */
