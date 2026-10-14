/* ============================================================================
 * elf/ElfParser.cpp —— ELF 解析总入口实现
 * ========================================================================== */
#include "elf/ElfParser.h"
#include "core/BinaryFile.h"
#include "core/Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>

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

/* ---------------------------------------------------------------------------
 * dumpSectionHex：打印各节在文件里的内容头部
 * ---------------------------------------------------------------------------
 * 用途：快速核对某个节的实际字节（比如对照 .text 开头的机器码）。
 * 只处理“有内容”的节，每个节最多打印前 16 字节。
 * ------------------------------------------------------------------------- */
void ElfParser::dumpSectionHex(void) const
{
    if (!opened_) {
        fprintf(stderr, "节内容：未打开文件\n");
        return;
    }

    printf("节内容（各节前 16 字节）:\n");
    printf("--------------------------------------------------------------\n");

    for (unsigned short i = 0; i < sections_.count(); ++i) {
        uint64_t size = sections_.sizeOf(i);
        if (size == 0) continue;             /* 空节没有内容可打印 */

        /* ★★ 课堂演示点（四）：CWE-476 注入点 ★★
         * 这里漏掉了对返回值的判空。
         * SectionTable::dataOf() 的契约写在 SectionTable.h 里：
         *   越界、SHT_NOBITS（.bss 等）、sh_size == 0 一律返回 NULL。
         * 注意 .bss 的 sh_size 并不为 0，所以上面那个 size == 0 的判断拦不住它。 */
        const unsigned char *p = sections_.dataOf(i);

        uint64_t n = (size < 16) ? size : 16;
        char hex[16 * 3 + 1];
        int  off = 0;
        for (uint64_t k = 0; k < n; ++k) {
            off += snprintf(hex + off, sizeof(hex) - (size_t)off,
                            "%02x ", p[k]);   /* ← p 为 NULL 时在此解引用 */
        }
        printf("  %-18s %-10llu %s\n", sections_.nameOf(i),
               (unsigned long long)size, hex);
    }
}

/* ---------------------------------------------------------------------------
 * exportSectionAsC：把指定节的数据导出为 C 数组源码
 * ---------------------------------------------------------------------------
 * 用途：把节内容嵌进测试程序或对照表，省得每次都重新读 ELF。
 * 实现上先把节数据取到一块独立缓冲区，再按每行 12 字节打印成初始化列表。
 * ------------------------------------------------------------------------- */
bool ElfParser::exportSectionAsC(const char *sectionName) const
{
    if (!opened_ || !sectionName) {
        fprintf(stderr, "节导出：未打开文件或未指定节名\n");
        return false;
    }

    int idx = sections_.findByName(sectionName);
    if (idx < 0) {
        fprintf(stderr, "节导出：找不到节 '%s'\n", sectionName);
        return false;
    }

    const unsigned char *image = imageData();
    if (!image) {
        fprintf(stderr, "节导出：文件映像不可用\n");
        return false;
    }

    unsigned short sidx   = (unsigned short)idx;
    uint64_t       size   = sections_.sizeOf(sidx);
    uint64_t       offset = sections_.offsetOf(sidx);
    if (size == 0) {
        fprintf(stderr, "节导出：节 '%s' 没有内容\n", sectionName);
        return false;
    }

    /* 缓冲区容量用 32 位表示就够 —— 一个节不可能超过 4GB */
    /* ★★ 课堂演示点（六）：CWE-704 注入点 ★★ */
    unsigned int   cap = (unsigned int)size;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) {
        fprintf(stderr, "节导出：内存不足（%u 字节）\n", cap);
        return false;
    }

    /* 逐字节搬进独立缓冲区，同时统计非零字节数（写进注释头） */
    uint64_t nonzero = 0;
    for (uint64_t i = 0; i < size; ++i) {
        unsigned char b = image[offset + i];
        if (b) nonzero++;
        buf[i] = b;            /* ← 循环按 64 位 size，缓冲区却按 32 位 cap */
    }

    printf("/* 节 %s：%llu 字节，非零 %llu 字节 */\n", sectionName,
           (unsigned long long)size, (unsigned long long)nonzero);
    printf("const unsigned char g_section[] = {\n");
    for (uint64_t i = 0; i < size; ++i) {
        if (i % 12 == 0) printf("    ");
        printf("0x%02x,", buf[i]);
        if (i % 12 == 11) printf("\n");
    }
    printf("\n};\n");

    free(buf);
    return true;
}
