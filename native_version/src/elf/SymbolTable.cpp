/* ============================================================================
 * elf/SymbolTable.cpp —— 符号表实现
 * ========================================================================== */
#include "elf/SymbolTable.h"
#include "elf/SectionTable.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

SymbolTable::SymbolTable()
    : symbols_(NULL), count_(0)
{
    sectionName_[0] = '\0';
}

bool SymbolTable::parse(const unsigned char *symData, size_t symBytes,
                        const unsigned char *strData, size_t strBytes,
                        const char *sectionName)
{
    symbols_ = NULL;
    count_   = 0;

    if (sectionName) {
        size_t len = strlen(sectionName);
        if (len >= sizeof(sectionName_)) len = sizeof(sectionName_) - 1;
        memcpy(sectionName_, sectionName, len);
        sectionName_[len] = '\0';
    } else {
        sectionName_[0] = '\0';
    }

    if (!symData || symBytes == 0) {
        logDebug("符号表 %s: 数据为空", sectionName_);
        return false;
    }
    if (symBytes % sizeof(Elf64_Sym) != 0) {
        logError("符号表 %s: 大小 %lu 不是 Elf64_Sym(%lu) 的整数倍，数据可能损坏",
                 sectionName_, (unsigned long)symBytes,
                 (unsigned long)sizeof(Elf64_Sym));
        return false;
    }
    if (!strData || strBytes == 0) {
        logWarn("符号表 %s: 缺少配套字符串表，符号名不可用", sectionName_);
    }

    symbols_ = (const Elf64_Sym *)symData;
    count_   = symBytes / sizeof(Elf64_Sym);
    strings_.bind(strData, strBytes);

    logDebug("符号表 %s: 解析出 %lu 个符号，其中函数 %lu 个",
             sectionName_, (unsigned long)count_,
             (unsigned long)functionCount());
    return true;
}

const Elf64_Sym *SymbolTable::at(size_t index) const
{
    if (!symbols_ || index >= count_) return NULL;
    return &symbols_[index];
}

const char *SymbolTable::nameOf(const Elf64_Sym *sym) const
{
    if (!sym) return NULL;
    if (!strings_.isValid()) return NULL;
    return strings_.get(sym->st_name);
}

const char *SymbolTable::nameOfIndex(size_t index) const
{
    return nameOf(at(index));
}

const char *SymbolTable::typeName(int type)
{
    switch (type) {
    case STT_NOTYPE:  return "NOTYPE";
    case STT_OBJECT:  return "OBJECT";
    case STT_FUNC:    return "FUNC";
    case STT_SECTION: return "SECTION";
    case STT_FILE:    return "FILE";
    case STT_COMMON:  return "COMMON";
    case STT_TLS:     return "TLS";
    default:          return "OTHER";
    }
}

const char *SymbolTable::bindName(int bind)
{
    switch (bind) {
    case STB_LOCAL:  return "LOCAL";
    case STB_GLOBAL: return "GLOBAL";
    case STB_WEAK:   return "WEAK";
    default:         return "OTHER";
    }
}

size_t SymbolTable::functionCount(void) const
{
    size_t n = 0;
    for (size_t i = 0; i < count_; ++i) {
        if (ELF64_ST_TYPE(symbols_[i].st_info) == STT_FUNC) n++;
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * 把符号表条目转换为 FunctionInfo：
 *   st_value 是虚拟地址，需要借助节表换算成"文件偏移 + 机器码指针"。
 * 同时处理三类边界情况：
 *   - 符号未定义（SHN_UNDEF）或特殊索引（SHN_ABS 等）→ 没有机器码；
 *   - st_size == 0 → 用"同节内下一个函数符号的地址"或"节末尾"兜底；
 *   - st_size 超出节范围 → 截断并告警。
 * ------------------------------------------------------------------------- */
bool SymbolTable::fillFunctionInfo(size_t index,
                                   const SectionTable &sections,
                                   const unsigned char *fileData,
                                   size_t fileSize,
                                   FunctionInfo *out) const
{
    const Elf64_Sym *sym = at(index);
    if (!sym || !out) return false;

    out->clear();

    const char *nm = nameOf(sym);
    if (nm) {
        size_t len = strlen(nm);
        if (len >= sizeof(out->name)) len = sizeof(out->name) - 1;
        memcpy(out->name, nm, len);
        out->name[len] = '\0';
    }

    out->virtualAddress = (uint64_t)sym->st_value;
    out->size           = (uint64_t)sym->st_size;
    out->symbolIndex    = (int)index;
    out->bind           = (unsigned char)bindOf(sym);

    /* ---- 判断符号是否真的对着某段代码 ---- */
    if (sym->st_shndx == SHN_UNDEF) {
        logDebug("符号 %s: 未定义（SHN_UNDEF），没有机器码", out->name);
        return true;
    }
    if (sym->st_shndx >= SHN_LORESERVE) {
        logDebug("符号 %s: 特殊节索引 %u（ABS/COMMON 等），没有机器码",
                 out->name, (unsigned)sym->st_shndx);
        return true;
    }

    int secIdx = (int)sym->st_shndx;
    out->sectionIndex = secIdx;

    uint64_t secAddr = sections.addressOf((unsigned short)secIdx);
    uint64_t secSize = sections.sizeOf((unsigned short)secIdx);
    uint64_t secOff  = sections.offsetOf((unsigned short)secIdx);
    const unsigned char *secData = sections.dataOf((unsigned short)secIdx);

    if (!secData) {
        logDebug("符号 %s: 所属节 %s 没有文件数据（如 .bss）",
                 out->name, sections.nameOf((unsigned short)secIdx));
        return true;
    }

    if ((uint64_t)sym->st_value < secAddr) {
        logWarn("符号 %s: 地址 0x%llx 小于所属节 %s 的起始地址 0x%llx，忽略",
                out->name, (unsigned long long)sym->st_value,
                sections.nameOf((unsigned short)secIdx),
                (unsigned long long)secAddr);
        return true;
    }

    uint64_t offsetInSection = (uint64_t)sym->st_value - secAddr;
    if (offsetInSection >= secSize) {
        logWarn("符号 %s: 节内偏移 %llu 超出节 %s 大小 %llu，忽略",
                out->name, (unsigned long long)offsetInSection,
                sections.nameOf((unsigned short)secIdx),
                (unsigned long long)secSize);
        return true;
    }

    uint64_t available = secSize - offsetInSection;
    uint64_t usable    = (uint64_t)sym->st_size;

    /* ---- st_size == 0 的兜底：找同节内下一个函数符号 ---- */
    if (usable == 0) {
        uint64_t nextAddr = 0;
        for (size_t i = 0; i < count_; ++i) {
            const Elf64_Sym *other = &symbols_[i];
            if (other == sym) continue;
            if (ELF64_ST_TYPE(other->st_info) != STT_FUNC) continue;
            if (other->st_shndx != sym->st_shndx) continue;
            if ((uint64_t)other->st_value <= (uint64_t)sym->st_value) continue;
            if (nextAddr == 0 || (uint64_t)other->st_value < nextAddr) {
                nextAddr = (uint64_t)other->st_value;
            }
        }
        if (nextAddr > (uint64_t)sym->st_value) {
            usable = nextAddr - (uint64_t)sym->st_value;
            logDebug("符号 %s: st_size 为 0，按下一个符号推算长度为 %llu 字节",
                     out->name, (unsigned long long)usable);
        } else {
            usable = available;
            logDebug("符号 %s: st_size 为 0 且找不到下一个符号，按节末尾推算长度为 %llu 字节",
                     out->name, (unsigned long long)usable);
        }
    } else if (usable > available) {
        logWarn("符号 %s: 声明大小 %llu 超出节内剩余空间 %llu，已截断",
                out->name, (unsigned long long)usable,
                (unsigned long long)available);
        usable = available;
    }

    out->size       = usable;
    out->fileOffset = secOff + offsetInSection;

    /* ---- 最终边界检查：机器码必须完整落在文件内 ---- */
    if (out->fileOffset > (uint64_t)fileSize ||
        usable > (uint64_t)fileSize - out->fileOffset) {
        logWarn("符号 %s: 机器码范围 [%llu, %llu) 超出文件大小 %lu，忽略",
                out->name, (unsigned long long)out->fileOffset,
                (unsigned long long)(out->fileOffset + usable),
                (unsigned long)fileSize);
        out->code = NULL;
        return true;
    }

    out->code = secData + offsetInSection;
    (void)fileData;    /* fileData 仅用于保持接口一致性 */
    return true;
}

bool SymbolTable::findFunction(const char *name,
                               const SectionTable &sections,
                               const unsigned char *fileData,
                               size_t fileSize,
                               FunctionInfo *out) const
{
    if (!name || !out || !symbols_) return false;

    for (size_t i = 0; i < count_; ++i) {
        const Elf64_Sym *sym = &symbols_[i];
        if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC) continue;

        const char *symName = nameOf(sym);
        if (!symName || strcmp(symName, name) != 0) continue;

        return fillFunctionInfo(i, sections, fileData, fileSize, out);
    }
    return false;
}

void SymbolTable::dump(size_t limit) const
{
    if (!symbols_) {
        fprintf(stderr, "%s: 不可用\n", sectionName_);
        return;
    }

    fprintf(stderr, "%s（共 %lu 个符号，其中函数 %lu 个）:\n",
            sectionName_, (unsigned long)count_,
            (unsigned long)functionCount());
    fprintf(stderr, "  %-6s %-8s %-8s %-18s %-10s %s\n",
            "Index", "Bind", "Type", "Value", "Size", "Name");

    size_t shown = 0;
    for (size_t i = 0; i < count_ && shown < limit; ++i) {
        const Elf64_Sym *sym = &symbols_[i];
        int type = ELF64_ST_TYPE(sym->st_info);
        int bind = ELF64_ST_BIND(sym->st_info);

        /* 只打印有意义的符号，跳过节符号和文件符号 */
        if (type != STT_FUNC && type != STT_OBJECT) continue;
        const char *nm = nameOf(sym);
        if (!nm || !*nm) continue;

        fprintf(stderr, "  %-6lu %-8s %-8s 0x%016llx %-10llu %s\n",
                (unsigned long)i, bindName(bind), typeName(type),
                (unsigned long long)sym->st_value,
                (unsigned long long)sym->st_size, nm);
        shown++;
    }
    if (shown == 0) fprintf(stderr, "  （没有可显示的函数/对象符号）\n");
}
