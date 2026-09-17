/* ============================================================================
 * output/TextWriter.cpp —— 文本输出实现
 * ========================================================================== */
#include "output/TextWriter.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "elf/FunctionInfo.h"
#include "core/Log.h"

#include <cstdio>

bool TextWriter::write(FILE *out, ControlFlowGraph *cfg)
{
    if (!out || !cfg) {
        logError("TextWriter: 参数无效");
        return false;
    }
    if (cfg->isEmpty()) {
        logError("TextWriter: CFG 为空");
        return false;
    }

    const FunctionInfo *fn = cfg->function();

    /* ---- 函数概览 ---- */
    fprintf(out, "========================================================\n");
    fprintf(out, "控制流图（文本形式）\n");
    fprintf(out, "========================================================\n");
    if (fn) {
        fprintf(out, "函数      : %s\n", fn->name);
        fprintf(out, "入口地址  : 0x%llx\n", (unsigned long long)fn->virtualAddress);
        fprintf(out, "函数大小  : %llu 字节\n", (unsigned long long)fn->size);
    }
    fprintf(out, "基本块    : %lu\n", (unsigned long)cfg->blockCount());
    fprintf(out, "边        : %lu\n", (unsigned long)cfg->edgeCount());
    fprintf(out, "指令      : %lu\n", (unsigned long)cfg->instructionCount());
    fprintf(out, "\n");

    /* ---- 基本块 ---- */
    fprintf(out, "--------------------------------------------------------\n");
    fprintf(out, "基本块\n");
    fprintf(out, "--------------------------------------------------------\n");

    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        fprintf(out, "\nB%d  [0x%llx, 0x%llx)  指令 %d 条  %s%s\n",
                b->id,
                (unsigned long long)b->startAddress,
                (unsigned long long)b->endAddress,
                b->instructionCount(),
                b->reachable ? "可达" : "不可达",
                (b == cfg->entryBlock()) ? "  <== 入口块" : "");

        if (showInstructions_) {
            for (Instruction *ins = b->first; ins != NULL; ins = ins->next) {
                fprintf(out, "      0x%llx:  %-8s %s\n",
                        (unsigned long long)ins->address,
                        ins->mnemonic, ins->operands);
                if (ins == b->last) break;
            }
        }
    }

    /* ---- 边 ---- */
    fprintf(out, "\n--------------------------------------------------------\n");
    fprintf(out, "基本块执行关系（边）\n");
    fprintf(out, "--------------------------------------------------------\n");

    for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
        if (!e->from) continue;

        fprintf(out, "  B%d -> ", e->from->id);
        if (e->to) {
            fprintf(out, "B%-3d  类型 %-14s 目标 0x%llx%s",
                    e->to->id, edgeTypeName(e->type),
                    (unsigned long long)e->targetAddress,
                    e->targetResolved ? "" : "（目标未解析）");
        } else {
            fprintf(out, "???   类型 %-14s 目标不可静态确定", edgeTypeName(e->type));
        }
        if (e->isBackwardHint) {
            fprintf(out, "  [向后跳转边]");
        }
        if (e->label && e->label[0]) {
            fprintf(out, "  (触发指令: %s)", e->label);
        }
        fprintf(out, "\n");
    }

    fprintf(out, "\n");
    logDebug("TextWriter: 输出完成");
    return true;
}
