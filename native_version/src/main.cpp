/* ============================================================================
 * elfcfg —— Linux 命令行 ELF 控制流图绘制工具（native 版本）
 * ----------------------------------------------------------------------------
 * 本程序不调用任何外部程序：ELF 解析、函数定位、指令解码、基本块划分、
 * CFG 构造全部由自身 C++ 代码实现。
 *
 * 【当前进度】
 *   阶段 1  ✅ core 层（BinaryFile/mmap、Log、MemoryArena、SimpleVector）
 *   阶段 2  ✅ ELF 文件头 / 节头表 / 字符串表
 *   阶段 3  ✅ 符号表 / 函数表 / 函数定位
 *   阶段 4~ 待接入：Instruction、x86-64 解码器、CFG、分析 Pass
 * ========================================================================== */
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

/* 命令行子命令 */
enum Command {
    CMD_CFG,          /* 默认：输出 Graphviz DOT（待阶段 9 接通） */
    CMD_INFO,         /* --info    : ELF 概览          */
    CMD_SECTIONS,     /* --sections: 节表              */
    CMD_SYMBOLS,      /* --symbols : 符号表 / 函数表   */
    CMD_DISASM,       /* --disasm  : 自己的反汇编结果 */
    CMD_SUMMARY,      /* --summary : 函数与 CFG 统计  */
    CMD_TEXT          /* --text    : 文本形式 CFG     */
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
        "  %s --disasm demo classify          查看自己的反汇编结果\n"
        "  %s --summary demo classify         查看统计信息\n"
        "\n"
        "通用选项:\n"
        "  --debug            打开调试日志\n"
        "  --log=<级别>       error | warn | info | debug\n"
        "  -h, --help         显示本帮助\n",
        prog, prog, prog, prog, prog, prog, prog);
}

/* 打印函数定位结果 */
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

    /* 打印前 16 字节机器码，确认定位正确 */
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

int main(int argc, char **argv)
{
    logSetPrefix("elfcfg");

    const char *elfPath  = NULL;
    const char *funcName = NULL;
    Command     command  = CMD_CFG;

    /* ---- 参数解析 ---- */
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
        } else if (strcmp(a, "--disasm") == 0) {
            command = CMD_DISASM;
        } else if (strcmp(a, "--summary") == 0) {
            command = CMD_SUMMARY;
        } else if (strcmp(a, "--text") == 0) {
            command = CMD_TEXT;
        } else if (strcmp(a, "--cfg") == 0) {
            command = CMD_CFG;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (a[0] == '-' && a[1] != '\0') {
            logError("未知选项：%s", a);
            printUsage(argv[0]);
            return 1;
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

    if (!elfPath) {
        printUsage(argv[0]);
        return 1;
    }

    /* ======================================================================
     * 1. 打开并解析 ELF（内部使用 BinaryFile 基类指针 → mmap 派生类）
     * ==================================================================== */
    ElfParser parser;
    if (!parser.open(elfPath)) {
        logError("解析失败：%s", parser.lastError());
        return 2;
    }

    /* ---- 纯信息类命令，不需要函数名 ---- */
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

    /* ---- 其余命令都需要函数名 ---- */
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

    /* ======================================================================
     * 2. 解码：机器码 → Instruction 链表
     *    这里通过"基类指针 → 派生类对象"调用解码器（运行时多态）
     * ==================================================================== */
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

    /* ---- 若是纯反汇编请求，打印后即可结束 ---- */
    if (command == CMD_DISASM) {
        printf("反汇编: 函数 %s（0x%llx，%llu 字节，共 %lu 条指令）\n",
               fn.name, (unsigned long long)fn.virtualAddress,
               (unsigned long long)fn.size, count);
        /* 机器码字段宽度按 x86 最长指令 15 字节算：15*3 = 45 字符 */
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

    /* ======================================================================
     * 3. 基本块划分（基类指针 → Leader 法派生类）
     * ==================================================================== */
    ControlFlowGraph cfg(&arena);
    cfg.setFunction(&fn);

    BasicBlockBuilder *builder = new LinearBasicBlockBuilder();
    if (!builder->build(&cfg, insns)) {
        logError("基本块划分失败");
        delete builder; delete decoder;
        return 5;
    }

    /* ======================================================================
     * 4. 基本块执行关系识别（基类指针 → x86 派生类）
     * ==================================================================== */
    EdgeAnalyzer *analyzer = new X86EdgeAnalyzer();
    if (!analyzer->analyze(&cfg)) {
        logError("执行关系分析失败");
        delete builder; delete analyzer; delete decoder;
        return 6;
    }

    /* ======================================================================
     * 5. 分析 Pass
     *    统一用基类指针数组驱动 —— 新增 Pass 不必改这里的主流程
     * ==================================================================== */
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

    /* ======================================================================
     * 6. 输出
     * ==================================================================== */
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
        /* 多态：基类指针 → 文本输出派生类 */
        GraphWriter *textWriter = new TextWriter();
        textWriter->write(stdout, &cfg);
        delete textWriter;
        break;
    }

    case CMD_CFG:
    default: {
        /* 多态：基类指针 → DOT 派生类，输出到 stdout */
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
    delete decoder;        /* 虚析构保证资源正确释放 */
    return 0;
}
