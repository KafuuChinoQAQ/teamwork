#include "core/Log.h"
#include "core/BinaryFile.h"
#include "core/MemoryArena.h"
#include "core/SimpleVector.h"
#include "elf/ElfParser.h"
#include "arch/Instruction.h"
#include "arch/InstructionDecoder.h"
#include "arch/InstructionStream.h"
#include "arch/x86/X86Decoder.h"
#include "arch/x86/X86OpcodeTable.h"
#include "cfg/ControlFlowGraph.h"
#include "cfg/BasicBlockBuilder.h"
#include "cfg/EdgeAnalyzer.h"
#include "analysis/AnalysisPass.h"
#include "analysis/ReachabilityPass.h"
#include "analysis/LoopHintPass.h"
#include "analysis/StatisticsPass.h"
#include "output/GraphWriter.h"
#include "output/DotGraphWriter.h"
#include "output/TextWriter.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

enum Command {
    CMD_CFG,
    CMD_INFO,
    CMD_SECTIONS,
    CMD_SYMBOLS,
    CMD_HEXDUMP,
    CMD_DISASM,
    CMD_SUMMARY,
    CMD_TEXT,
    CMD_BATCH,
    CMD_DIFF
};

static void printUsage(const char *prog)
{
    fprintf(stderr,
        "用法: %s [选项] <ELF文件> [函数名]\n"
        "\n"
        "示例:\n"
        "  %s demo classify > classify.dot    生成控制流图 DOT（默认）\n"
        "  %s --info demo                     查看 ELF 概览\n"
        "  %s --sections demo                 查看节表\n"
        "  %s --symbols demo                  查看符号表 / 函数表\n"
        "  %s --hexdump demo                  查看各节的文件内容头部\n"
        "  %s --disasm demo classify          查看自己的反汇编结果\n"
        "  %s --summary demo classify         查看统计信息\n"
        "  %s --batch classify a.elf b.elf    批量分析多个文件\n"
        "  %s --diff  classify a.elf b.elf    对比两个文件中的同名函数\n"
        "\n"
        "通用选项:\n"
        "  --debug            打开调试日志\n"
        "  --log=<级别>       error | warn | info | debug\n"
        "  -h, --help         显示本帮助\n",
        prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static void printFunctionInfo(const FunctionInfo &fn)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "函数:\n");
    fprintf(stderr, "  名字      : %s\n", fn.name);
    fprintf(stderr, "  虚拟地址  : 0x%llx\n", (unsigned long long)fn.virtualAddress);
    fprintf(stderr, "  大小      : %llu 字节\n", (unsigned long long)fn.size);
    fprintf(stderr, "  文件偏移  : 0x%llx\n", (unsigned long long)fn.fileOffset);
    fprintf(stderr, "  所属节索引: %d\n", fn.sectionIndex);
    fprintf(stderr, "  符号索引  : %d\n", fn.symbolIndex);
    fprintf(stderr, "  机器码指针: %p\n", (const void *)fn.code);

    if (fn.code && fn.size > 0) {
        size_t n = (fn.size < 16) ? (size_t)fn.size : 16;
        char hex[16 * 3 + 1];
        int  off = 0;
        for (size_t i = 0; i < n; ++i) {
            off += snprintf(hex + off, sizeof(hex) - (size_t)off,
                            "%02x ", fn.code[i]);
        }
        fprintf(stderr, "  开头字节  : %s\n", hex);
    }
}

#define kMaxBatchArgs 32
#define kMaxBatch     30

struct BatchItem {
    const char         *path;
    const FunctionInfo *fn;
    unsigned char      *codeCopy;
    size_t              codeSize;
};

static unsigned char *dupCode(const unsigned char *src, size_t size)
{
    const unsigned char *from = src;
    if (!from || size == 0) return NULL;

    unsigned char *buf = (unsigned char *)malloc(size);
    unsigned char *to  = buf;
    if (!to) return NULL;

    memcpy(to, from, size);
    return to;
}

static void releaseOne(BatchItem *row)
{
    unsigned char *buf = row->codeCopy;
    if (buf) {
        free(buf);
    }
}

static void freeCodeCopies(BatchItem *items, int n)
{
    BatchItem *rows = items;

    for (int i = 0; i < n; ++i) {
        BatchItem *row = rows + i;
        releaseOne(row);
    }
}

static int runBatch(const char *funcName, const char *const *files, int nfiles)
{
    ElfParser parser;
    BatchItem items[kMaxBatch];
    int       n    = 0;
    int       nOk  = 0;
    int       nHit = 0;
    int       nFail = 0;

    printf("批量分析：函数 '%s'，共 %d 个文件\n", funcName, nfiles);

    for (int i = 0; i < nfiles && n < kMaxBatch; i++) {
        const char *path = files[i];
        BatchItem it;
        it.path     = path;
        it.fn       = NULL;
        it.codeCopy = NULL;
        it.codeSize = 0;

        if (!parser.open(path)) {
            logWarn("跳过 %s：%s", path, parser.lastError());
            nFail++;
            items[n++] = it;
            continue;
        }
        nOk++;

        const FunctionTable &tbl = parser.functions();
        const FunctionInfo  *hit = tbl.findByName(funcName);
        it.fn = hit;

        if (hit) {
            nHit++;
            const unsigned char *code = hit->code;
            size_t               len  = (size_t)hit->size;
            unsigned char       *copy = dupCode(code, len);
            it.codeCopy = copy;
            it.codeSize = copy ? len : 0;
        }

        items[n++] = it;
    }

    printf("\n%-30s %-18s %8s %10s   %s\n",
           "文件", "函数入口地址", "大小", "机器码首字节", "机器码指针");
    printf("----------------------------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        const BatchItem    *row  = &items[i];
        const char         *path = row->path;
        const FunctionInfo *f    = row->fn;

        if (!f) {
            printf("%-30s %s\n", path, "(该文件没有此函数 / 解析失败)");
            continue;
        }

        const unsigned char *code = f->code;
        uint64_t             addr = f->virtualAddress;
        uint64_t             size = f->size;

        printf("%-30s 0x%-16llx %8llu %10s   %p\n",
               path,
               (unsigned long long)addr,
               (unsigned long long)size,
               (code && size) ? "——" : "(无)",
               (const void *)code);
    }

    printf("\n汇总：解析成功 %d 个，定位到 '%s' 的 %d 个（共 %d 个文件）\n",
           nOk, funcName, nHit, nfiles);

    printf("\n机器码校验（读取各函数首字节）：\n");
    for (int i = 0; i < n; i++) {
        const BatchItem     *row  = &items[i];
        const unsigned char *buf  = row->codeCopy;
        const char          *path = row->path;
        if (!buf || row->codeSize < 4) continue;

        printf("  %-30s 首字节 = %02x %02x %02x %02x\n",
               path, buf[0], buf[1], buf[2], buf[3]);
    }

    BatchItem *rows = items;

    if (nFail > 0) {
        logWarn("有 %d 个文件解析失败，本次批量分析结果不完整", nFail);
        freeCodeCopies(rows, n);
    }

    freeCodeCopies(rows, n);

    printf("\n批处理完成。\n");
    return 0;
}

static int runDiff(const char *funcName, const char *fileA, const char *fileB)
{
    ElfParser parser;

    if (!parser.open(fileA)) {
        logError("无法解析基准文件 %s：%s", fileA, parser.lastError());
        return 1;
    }
    const FunctionTable &tblA = parser.functions();
    const FunctionInfo  *fa   = tblA.findByName(funcName);
    if (!fa) {
        logError("基准文件 %s 中没有函数 '%s'", fileA, funcName);
        return 1;
    }
    const FunctionInfo *base   = fa;
    unsigned            nSecA  = parser.sections().count();
    uint64_t            entryA = parser.header().entry();

    printf("基准文件 %s\n", fileA);
    printf("  函数 %s @ 0x%llx，%llu 字节，节数量 %u，入口 0x%llx\n",
           base->name, (unsigned long long)base->virtualAddress,
           (unsigned long long)base->size, nSecA,
           (unsigned long long)entryA);

    if (!parser.open(fileB)) {
        logError("无法解析对比文件 %s：%s", fileB, parser.lastError());
        return 1;
    }
    const FunctionTable &tblB = parser.functions();
    const FunctionInfo  *fb   = tblB.findByName(funcName);
    if (!fb) {
        logError("对比文件 %s 中没有函数 '%s'", fileB, funcName);
        return 1;
    }
    printf("\n对比文件 %s\n", fileB);
    printf("  函数 %s @ 0x%llx，%llu 字节，节数量 %u，入口 0x%llx\n",
           fb->name, (unsigned long long)fb->virtualAddress,
           (unsigned long long)fb->size, parser.sections().count(),
           (unsigned long long)parser.header().entry());

    printf("\n%-14s %-18s %10s %10s\n", "来源", "函数地址", "大小", "机器码");
    printf("----------------------------------------------------------------\n");
    printf("%-14s 0x%-16llx %10llu %10s\n", "基准",
           (unsigned long long)base->virtualAddress,
           (unsigned long long)base->size,
           (base->code && base->size) ? "可读" : "无");
    printf("%-14s 0x%-16llx %10llu %10s\n", "对比",
           (unsigned long long)fb->virtualAddress,
           (unsigned long long)fb->size,
           (fb->code && fb->size) ? "可读" : "无");

    if (base->size == fb->size) {
        printf("\n结论：两个版本中该函数大小相同（%llu 字节）\n",
               (unsigned long long)fb->size);
    } else {
        printf("\n结论：大小相差 %+lld 字节\n",
               (long long)fb->size - (long long)base->size);
    }

    if (base->code && fb->code && base->size == fb->size && base->size > 0) {
        size_t differ = 0;
        for (size_t i = 0; i < (size_t)base->size; i++) {
            if (base->code[i] != fb->code[i]) differ++;
        }
        printf("机器码：%lu / %llu 字节不同\n",
               (unsigned long)differ, (unsigned long long)base->size);
        printf("首字节：基准 %02x %02x %02x %02x   对比 %02x %02x %02x %02x\n",
               base->code[0], base->code[1], base->code[2], base->code[3],
               fb->code[0], fb->code[1], fb->code[2], fb->code[3]);
    }

    printf("\n对比完成。\n");
    return 0;
}

int main(int argc, char **argv)
{
    logSetPrefix("elfcfg");

    const char *elfPath  = NULL;
    const char *funcName = NULL;
    Command     command  = CMD_CFG;

    const char *posArg[kMaxBatchArgs];
    int         posCount = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        if (strcmp(a, "--debug") == 0) {
            logSetLevel(LOG_LEVEL_DEBUG);
        } else if (strncmp(a, "--log=", 6) == 0) {
            LogLevel lv;
            if (!logParseLevel(a + 6, &lv)) {
                logError("无法识别的日志级别：%s", a + 6);
                return 1;
            }
            logSetLevel(lv);
        } else if (strcmp(a, "--info") == 0) {
            command = CMD_INFO;
        } else if (strcmp(a, "--sections") == 0) {
            command = CMD_SECTIONS;
        } else if (strcmp(a, "--symbols") == 0) {
            command = CMD_SYMBOLS;
        } else if (strcmp(a, "--hexdump") == 0) {
            command = CMD_HEXDUMP;
        } else if (strcmp(a, "--disasm") == 0) {
            command = CMD_DISASM;
        } else if (strcmp(a, "--summary") == 0) {
            command = CMD_SUMMARY;
        } else if (strcmp(a, "--text") == 0) {
            command = CMD_TEXT;
        } else if (strcmp(a, "--cfg") == 0) {
            command = CMD_CFG;
        } else if (strcmp(a, "--batch") == 0) {
            command = CMD_BATCH;
        } else if (strcmp(a, "--diff") == 0) {
            command = CMD_DIFF;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (a[0] == '-' && a[1] != '\0') {
            logError("未知选项：%s", a);
            printUsage(argv[0]);
            return 1;
        } else if (command == CMD_BATCH || command == CMD_DIFF) {
            if (posCount >= kMaxBatchArgs) {
                logError("批处理参数过多（上限 %d）", kMaxBatchArgs);
                return 1;
            }
            posArg[posCount++] = a;
        } else if (!elfPath) {
            elfPath = a;
        } else if (!funcName) {
            funcName = a;
        } else {
            logError("参数过多：%s", a);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (command == CMD_DIFF) {
        if (posCount < 3) {
            logError("对比用法：%s --diff <函数名> <ELF文件A> <ELF文件B>", argv[0]);
            return 1;
        }
        return runDiff(posArg[0], posArg[1], posArg[2]);
    }

    if (command == CMD_BATCH) {
        if (posCount < 2) {
            logError("批处理用法：%s --batch <函数名> <ELF文件> [ELF文件 ...]",
                     argv[0]);
            return 1;
        }
        return runBatch(posArg[0], &posArg[1], posCount - 1);
    }

    if (!elfPath) {
        printUsage(argv[0]);
        return 1;
    }

    ElfParser parser;
    if (!parser.open(elfPath)) {
        logError("解析失败：%s", parser.lastError());
        return 2;
    }

    if (command == CMD_INFO) {
        parser.dumpInfo();
        return 0;
    }
    if (command == CMD_SECTIONS) {
        parser.dumpSections();
        return 0;
    }
    if (command == CMD_SYMBOLS) {
        parser.dumpSymbols();
        parser.dumpFunctions();
        return 0;
    }
    if (command == CMD_HEXDUMP) {
        parser.dumpSectionHex();
        return 0;
    }

    if (!funcName) {
        logError("缺少函数名。用法：%s <ELF文件> <函数名>", argv[0]);
        return 1;
    }

    FunctionInfo fn;
    if (!parser.findFunction(funcName, &fn)) {
        logError("在 %s 中找不到函数 '%s'", elfPath, funcName);
        logError("提示：可用 --symbols 查看该文件包含哪些函数");
        return 3;
    }

    printFunctionInfo(fn);

    MemoryArena arena;

    InstructionDecoder *decoder = new X86_64Decoder();

    unsigned long count    = 0;
    size_t        consumed = 0;
    Instruction  *insns = decodeInstructionStream(decoder, fn.code, (size_t)fn.size,
                                                  fn.virtualAddress, &arena,
                                                  &count, &consumed);

    if (!insns) {
        logError("解码失败：%s", decoder->lastError());
        delete decoder;
        return 4;
    }

    if (command == CMD_DISASM) {
        printf("反汇编: 函数 %s（0x%llx，%llu 字节，共 %lu 条指令）\n",
               fn.name, (unsigned long long)fn.virtualAddress,
               (unsigned long long)fn.size, count);
        printf("%-12s %-47s %s\n", "地址", "机器码", "指令");

        for (Instruction *ins = insns; ins != NULL; ins = ins->next) {
            char hex[16 * 3 + 1];
            int  off = 0;
            for (unsigned int i = 0; i < ins->length && i < 16; ++i) {
                off += snprintf(hex + off, sizeof(hex) - (size_t)off,
                                "%02x ", ins->bytes[i]);
            }
            printf("0x%-10llx %-47s %s %s\n",
                   (unsigned long long)ins->address, hex,
                   ins->mnemonic, ins->operands);
        }
        delete decoder;
        return 0;
    }

    ControlFlowGraph cfg(&arena);
    cfg.setFunction(&fn);

    BasicBlockBuilder *builder = new LinearBasicBlockBuilder();
    if (!builder->build(&cfg, insns)) {
        logError("基本块划分失败");
        delete builder; delete decoder;
        return 5;
    }

    EdgeAnalyzer *analyzer = new X86EdgeAnalyzer();
    if (!analyzer->analyze(&cfg)) {
        logError("执行关系分析失败");
        delete builder; delete analyzer; delete decoder;
        return 6;
    }

    ReachabilityPass reachability;
    LoopHintPass     loopHint;
    StatisticsPass   statistics;

    AnalysisPass *passes[3];
    passes[0] = &reachability;
    passes[1] = &loopHint;
    passes[2] = &statistics;

    const int passCount = 3;
    for (int i = 0; i < passCount; ++i) {
        logDebug("运行分析 Pass: %s —— %s",
                 passes[i]->name(), passes[i]->description());
        if (!passes[i]->run(&cfg)) {
            logWarn("分析 Pass '%s' 执行失败", passes[i]->name());
        }
    }

    switch (command) {
    case CMD_SUMMARY:
        cfg.printSummary();
        printf("\n");
        statistics.print();
        printf("\n");
        loopHint.printHints(&cfg);
        printf("\n");
        cfg.printBlocks();
        break;

    case CMD_TEXT: {
        GraphWriter *textWriter = new TextWriter();
        textWriter->write(stdout, &cfg);
        delete textWriter;
        break;
    }

    case CMD_CFG:
    default: {
        GraphWriter *dotWriter = new DotGraphWriter();
        bool ok = dotWriter->write(stdout, &cfg);
        fflush(stdout);
        delete dotWriter;

        if (!ok) {
            logError("DOT 输出失败");
            delete builder; delete analyzer; delete decoder;
            return 7;
        }
        break;
    }
    }

    delete builder;
    delete analyzer;
    delete decoder;
    return 0;
}
