/* ============================================================================
 * analysis/StatisticsPass.cpp —— 统计实现
 * ========================================================================== */
#include "analysis/StatisticsPass.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

StatisticsPass::StatisticsPass()
{
    memset(&stats_, 0, sizeof(stats_));
}

bool StatisticsPass::run(ControlFlowGraph *cfg)
{
    memset(&stats_, 0, sizeof(stats_));

    if (!cfg || cfg->isEmpty()) {
        logError("StatisticsPass: CFG 为空");
        return false;
    }

    /* ---- 逐块、逐指令统计 ---- */
    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        stats_.blocks++;
        if (!b->reachable) stats_.unreachableBlocks++;

        int inBlock = 0;
        for (Instruction *ins = b->first; ins != NULL; ins = ins->next) {
            stats_.instructions++;
            inBlock++;

            if (ins->isConditional)  stats_.conditionalBranches++;
            if (ins->isCall)         stats_.calls++;
            if (ins->isReturn)       stats_.returns++;
            if (ins->isIndirect)     stats_.indirectJumps++;

            if (ins == b->last) break;
        }
        if ((size_t)inBlock > stats_.maxBlockInstructions) {
            stats_.maxBlockInstructions = (size_t)inBlock;
        }
    }

    /* ---- 边的统计 ---- */
    for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
        stats_.edges++;
        if (e->isBackwardHint) stats_.backwardHints++;
    }

    logDebug("StatisticsPass: 指令 %lu / 块 %lu / 边 %lu",
             (unsigned long)stats_.instructions,
             (unsigned long)stats_.blocks,
             (unsigned long)stats_.edges);
    return true;
}

void StatisticsPass::print(void) const
{
    printf("统计信息:\n");
    printf("  指令数        : %lu\n", (unsigned long)stats_.instructions);
    printf("  基本块数      : %lu\n", (unsigned long)stats_.blocks);
    printf("  边数          : %lu\n", (unsigned long)stats_.edges);
    printf("  条件分支      : %lu\n", (unsigned long)stats_.conditionalBranches);
    printf("  call          : %lu\n", (unsigned long)stats_.calls);
    printf("  ret           : %lu\n", (unsigned long)stats_.returns);
    printf("  间接跳转      : %lu\n", (unsigned long)stats_.indirectJumps);
    printf("  向后跳转边    : %lu\n", (unsigned long)stats_.backwardHints);
    printf("  不可达块      : %lu\n", (unsigned long)stats_.unreachableBlocks);
    printf("  最大块指令数  : %lu\n", (unsigned long)stats_.maxBlockInstructions);
}
