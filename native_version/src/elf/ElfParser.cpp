/* ============================================================================
 * elf/ElfParser.cpp —— ELF 解析总入口实现
 * ========================================================================== */
#include "elf/ElfParser.h"
#include "core/BinaryFile.h"
#include "core/Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

ElfParser::ElfParser()
    : file_(NULL), opened_(false)
{
    error_[0] = '\0';
}

ElfParser::~ElfParser()
{
    close();
}

void ElfParser::setError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_, sizeof(error_), fmt, ap);
    va_end(ap);
}

/* ---------------------------------------------------------------------------
 * open：完整解析流程
 * ------------------------------------------------------------------------- */
bool ElfParser::open(const char *path)
{
    close();
    error_[0] = '\0';

    /* ---- 1. 文件访问层：基类指针 → 派生类对象（运行时多态） ---- */
    file_ = new MappedBinaryFile();
    if (!file_) {
        setError("内存不足，无法创建文件对象");
        logError("%s", error_);
        return false;
    }
    if (!file_->openFile(path)) {
        setError("无法打开文件 '%s'", path ? path : "(null)");
        delete file_;
        file_ = NULL;
        return false;
    }

    /* ---- 2. ELF 文件头 ---- */
    if (!header_.parse(file_->data(), file_->size())) {
        setError("%s", header_.errorText());
        logError("ELF 文件头解析失败：%s", header_.errorText());
        delete file_;
        file_ = NULL;
        return false;
    }

    /* ---- 3. 节头表 ---- */
    if (!parseSections()) {
        /* SectionTable 内部已给出具体错误 */
        delete file_;
        file_ = NULL;
        return false;
    }

    /* ---- 4. 符号表 ---- */
    parseSymbolTables();       /* 失败不致命：可能只是没有符号 */

    opened_ = true;

    logInfo("ELF 解析  : %s，%lu 字节，入口 0x%llx，%u 个节",
            path, (unsigned long)file_->size(),
            (unsigned long long)header_.entry(),
            (unsigned)header_.sectionHeaderCount());
    return true;
}

bool ElfParser::parseSections(void)
{
    if (!sections_.parse(file_->data(), file_->size(), header_)) {
        setError("节头表解析失败");
        return false;
    }
    return true;
}

bool ElfParser::parseSymbolTables(void)
{
    /* ---- .symtab + .strtab ---- */
    int symIdx = sections_.symtabIndex();
    int strIdx = sections_.strtabIndex();
    if (symIdx >= 0 && strIdx >= 0) {
        const unsigned char *symData = sections_.dataOf((unsigned short)symIdx);
        uint64_t symSize = sections_.sizeOf((unsigned short)symIdx);
        const unsigned char *strData = sections_.dataOf((unsigned short)strIdx);
        uint64_t strSize = sections_.sizeOf((unsigned short)strIdx);

        if (symData && strData) {
            symtab_.parse(symData, (size_t)symSize,
                          strData, (size_t)strSize, ".symtab");
        } else {
            logWarn("ElfParser: .symtab/.strtab 数据不可读");
        }
    } else {
        logDebug("ElfParser: 没有 .symtab（可能被 strip）");
    }

    /* ---- .dynsym + .dynstr ---- */
    int dsymIdx = sections_.dynsymIndex();
    int dstrIdx = sections_.dynstrIndex();
    if (dsymIdx >= 0 && dstrIdx >= 0) {
        const unsigned char *symData = sections_.dataOf((unsigned short)dsymIdx);
        uint64_t symSize = sections_.sizeOf((unsigned short)dsymIdx);
        const unsigned char *strData = sections_.dataOf((unsigned short)dstrIdx);
        uint64_t strSize = sections_.sizeOf((unsigned short)dstrIdx);

        if (symData && strData) {
            dynsym_.parse(symData, (size_t)symSize,
                          strData, (size_t)strSize, ".dynsym");
        }
    }

    /* ---- 构建函数表：优先 .symtab（更完整），否则退回 .dynsym ---- */
    const SymbolTable &primary = symtab_.isValid() ? symtab_ : dynsym_;
    if (primary.isValid()) {
        functions_.build(primary, sections_, file_->data(), file_->size());
    } else {
        logWarn("ElfParser: 该文件没有可用的符号表（可能被 strip）");
        return false;
    }
    return true;
}

void ElfParser::close(void)
{
    if (file_) {
        delete file_;         /* 虚析构 → MappedBinaryFile::closeFile() */
        file_ = NULL;
    }
    opened_ = false;
}

const unsigned char *ElfParser::imageData(void) const
{
    return file_ ? file_->data() : NULL;
}

size_t ElfParser::imageSize(void) const
{
    return file_ ? file_->size() : 0;
}

bool ElfParser::findFunction(const char *name, FunctionInfo *out) const
{
    if (!opened_ || !name || !out) return false;

    /* 先查 .symtab */
    if (symtab_.isValid()) {
        if (symtab_.findFunction(name, sections_, file_->data(),
                                 file_->size(), out) && out->hasCode()) {
            return true;
        }
    }
    /* 再查 .dynsym */
    if (dynsym_.isValid()) {
        if (dynsym_.findFunction(name, sections_, file_->data(),
                                 file_->size(), out) && out->hasCode()) {
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * 打印接口
 * ------------------------------------------------------------------------- */
void ElfParser::dumpInfo(void) const
{
    if (!opened_) {
        fprintf(stderr, "ELF 信息：未打开文件\n");
        return;
    }
    header_.dump();
    fprintf(stderr, "  节数量    : %u\n", (unsigned)sections_.count());
    fprintf(stderr, "  符号数量  : .symtab=%lu  .dynsym=%lu\n",
            (unsigned long)symtab_.count(), (unsigned long)dynsym_.count());
    fprintf(stderr, "  函数数量  : %lu\n",
            (unsigned long)functions_.count());
    fprintf(stderr, "  文件路径  : %s\n", file_->path());
    fprintf(stderr, "  文件大小  : %lu 字节\n", (unsigned long)file_->size());
}

void ElfParser::dumpSections(void) const
{
    if (!opened_) {
        fprintf(stderr, "节表：未打开文件\n");
        return;
    }
    sections_.dump();
}

void ElfParser::dumpSymbols(void) const
{
    if (!opened_) {
        fprintf(stderr, "符号表：未打开文件\n");
        return;
    }
    if (symtab_.isValid()) symtab_.dump(200);
    else fprintf(stderr, ".symtab：不可用（可能被 strip）\n");

    if (dynsym_.isValid()) dynsym_.dump(50);
}

void ElfParser::dumpFunctions(void) const
{
    if (!opened_) {
        fprintf(stderr, "函数表：未打开文件\n");
        return;
    }
    functions_.dump(200);
}
