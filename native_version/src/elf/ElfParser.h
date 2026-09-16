/* ============================================================================
 * elf/ElfParser.h —— ELF 解析总入口
 * ----------------------------------------------------------------------------
 * 把各层串起来，对外只暴露一个简洁接口：
 *
 *     ElfParser parser;
 *     parser.open("demo");                       // 打开 + 完整解析
 *     FunctionInfo fn;
 *     parser.findFunction("classify", &fn);      // 定位函数
 *     // fn.code / fn.size 就是交给解码器的机器码
 *
 * 内部分层（每层都可以单独使用、单独测试）：
 *
 *     BinaryFile          —— 文件映射（core 层）
 *     ElfHeaderView       —— 文件头解析与校验
 *     SectionTable        —— 节头表 + 节名
 *     StringTable         —— 字符串表
 *     SymbolTable         —— .symtab / .dynsym
 *     FunctionTable       —— STT_FUNC 汇总与查找
 *
 * 本类不调用任何外部程序，也不依赖 libelf / libbfd。
 * ========================================================================== */
#ifndef ELFCFG_ELF_ELFPARSER_H
#define ELFCFG_ELF_ELFPARSER_H

#include <cstddef>
#include <cstdint>

#include "elf/ElfHeaderView.h"
#include "elf/SectionTable.h"
#include "elf/StringTable.h"
#include "elf/SymbolTable.h"
#include "elf/FunctionTable.h"

class BinaryFile;

class ElfParser {
public:
    ElfParser();
    ~ElfParser();

    /* ---- 生命周期 ---- */
    bool open(const char *path);      /* 打开并完成全部解析 */
    void close(void);
    bool isOpen(void) const { return opened_; }
    const char *lastError(void) const { return error_; }

    /* ---- 各层访问（只读） ---- */
    BinaryFile          *file(void)     const { return file_;     }
    const ElfHeaderView &header(void)   const { return header_;   }
    const SectionTable  &sections(void) const { return sections_; }
    const SymbolTable   &symtab(void)   const { return symtab_;   }
    const SymbolTable   &dynsym(void)   const { return dynsym_;   }
    const FunctionTable &functions(void)const { return functions_;}

    /* 机器码所在的内存与长度（用于解码器的边界检查） */
    const unsigned char *imageData(void) const;
    size_t               imageSize(void) const;

    /* ---- 函数定位：先查 .symtab，再查 .dynsym ---- */
    bool findFunction(const char *name, FunctionInfo *out) const;

    /* ---- 打印（供命令行 --info / --sections / --symbols） ---- */
    void dumpInfo(void) const;
    void dumpSections(void) const;
    void dumpSymbols(void) const;
    void dumpFunctions(void) const;

private:
    BinaryFile   *file_;        /* 基类指针 → MappedBinaryFile（多态） */
    ElfHeaderView header_;
    SectionTable  sections_;
    SymbolTable   symtab_;
    SymbolTable   dynsym_;
    FunctionTable functions_;

    bool  opened_;
    char  error_[256];

    bool parseSections(void);
    bool parseSymbolTables(void);
    void setError(const char *fmt, ...);

    ElfParser(const ElfParser &);
    ElfParser &operator=(const ElfParser &);
};

#endif /* ELFCFG_ELF_ELFPARSER_H */
