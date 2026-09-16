/* ============================================================================
 * output/DotGraphWriter.cpp —— DOT 输出实现
 * ========================================================================== */
#include "output/DotGraphWriter.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "elf/FunctionInfo.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

/* ---------------------------------------------------------------------------
 * GraphWriter 的公共工具
 * ------------------------------------------------------------------------- */
void GraphWriter::writeEscaped(FILE *out, const char *s)
{
    if (!s) return;
    for (const char *p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') fputc('\\', out);
        fputc(*p, out);
    }
}

void GraphWriter::writeQuoted(FILE *out, const char *s)
{
    fputc('"', out);
    writeEscaped(out, s);
    fputc('"', out);
}

int GraphWriter::dotEscapeText(char *dst, int dstSize, const char *src)
{
    int n = 0;
    if (dstSize <= 0) return 0;
    for (const char *p = src; *p; ++p) {
        if (n >= dstSize - 2) break;
        if (*p == '"' || *p == '\\') dst[n++] = '\\';
        dst[n++] = *p;
    }
    dst[n] = '\0';
    return n;
}

int GraphWriter::isExitBlock(const void *block)
{
    const BasicBlock *b = (const BasicBlock *)block;
    if (!b || !b->last) return 0;
    return (b->last->type == INST_RET || b->last->type == INST_HLT) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * DotGraphWriter
 * ------------------------------------------------------------------------- */
bool DotGraphWriter::write(FILE *out, ControlFlowGraph *cfg)
{
    if (!out || !cfg) {
        logError("DotGraphWriter: 参数无效");
        return false;
    }
    if (cfg->isEmpty()) {
        logError("DotGraphWriter: CFG 为空，没有可输出的内容");
        return false;
    }

    const FunctionInfo *fn = cfg->function();
    const char *funcName = fn ? fn->name : "unknown";

    /* ---- 图头 ---- */
    fputs("digraph CFG {\n", out);
    fprintf(out, "    /* 由 elfcfg 自动生成 —— 函数 %s 的控制流图 */\n", funcName);
    fprintf(out, "    /* 入口: 0x%llx  基本块: %lu  边: %lu */\n",
            fn ? (unsigned long long)fn->virtualAddress : 0ULL,
            (unsigned long)cfg->blockCount(),
            (unsigned long)cfg->edgeCount());
    fputs("    rankdir=TB;\n", out);
    fputs("    labelloc=\"t\";\n", out);
    fputs("    fontname=\"Helvetica\";\n", out);
    fputs("    label=\"Control Flow Graph of ", out);
    writeEscaped(out, funcName);
    fputs("()\"\n", out);
    fputs("    node [shape=box, fontname=\"Courier New\", fontsize=10, "
          "style=filled, fillcolor=\"#eef4ff\", color=\"#3b6ea5\"];\n", out);
    fputs("    edge [fontname=\"Helvetica\", fontsize=9, color=\"#333333\"];\n\n", out);

    /* ---- 节点 ---- */
    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        char label[8192];
        int  off = 0;

        off += snprintf(label + off, (size_t)((int)sizeof(label) - off),
                        "B%d\\l", b->id);

        for (Instruction *ins = b->first; ins != NULL; ins = ins->next) {
            char buf[512];
            char esc[1100];

            if (ins->operands[0] != '\0') {
                snprintf(buf, sizeof(buf), "0x%llx:  %s %s",
                         (unsigned long long)ins->address,
                         ins->mnemonic, ins->operands);
            } else {
                snprintf(buf, sizeof(buf), "0x%llx:  %s",
                         (unsigned long long)ins->address, ins->mnemonic);
            }
            dotEscapeText(esc, (int)sizeof(esc), buf);

            if ((int)sizeof(label) - off < 64) {
                off += snprintf(label + off,
                                (size_t)((int)sizeof(label) - off), "...\\l");
                break;
            }
            off += snprintf(label + off, (size_t)((int)sizeof(label) - off),
                            "%s\\l", esc);

            if (ins == b->last) break;
        }

        fprintf(out, "    B%d [label=\"%s\"", b->id, label);

        /* 配色：入口绿、出口红、不可达灰 */
        if (b == cfg->entryBlock()) {
            fputs(", fillcolor=\"#d8f0d8\", color=\"#2e7d32\"", out);
        } else if (isExitBlock(b)) {
            fputs(", fillcolor=\"#ffe0e0\", color=\"#b71c1c\"", out);
        } else if (!b->reachable) {
            fputs(", fillcolor=\"#eeeeee\", color=\"#999999\", style=\"filled,dashed\"", out);
        }

        fputs("];\n", out);
    }

    fputs("\n", out);

    /* ---- 边 ---- */
    for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
        if (!e->from) continue;

        if (e->to == NULL) {
            /* 间接跳转等无法解析目标的边：画成一个悬挂的注释节点 */
            fprintf(out, "    B%d -> I%d [label=", e->from->id, e->from->id);
            writeQuoted(out, (e->label && e->label[0]) ? e->label : "indirect");
            fputs(", style=dotted, color=\"#8a6d00\", fontcolor=\"#8a6d00\", "
                  "constraint=false];\n", out);
            continue;
        }

        fprintf(out, "    B%d -> B%d [label=", e->from->id, e->to->id);
        writeQuoted(out, (e->label && e->label[0]) ? e->label : edgeTypeName(e->type));

        if (e->isBackwardHint) {
            fputs(", color=\"#cc0000\", fontcolor=\"#cc0000\", "
                  "penwidth=2, style=bold, constraint=false", out);
        } else if (e->type == EDGE_CALL) {
            fputs(", color=\"#00695c\", style=dashed", out);
        }
        fputs("];\n", out);
    }

    fputs("}\n", out);

    logInfo("DOT 输出 : %lu 个节点，%lu 条边",
            (unsigned long)cfg->blockCount(), (unsigned long)cfg->edgeCount());
    return true;
}
