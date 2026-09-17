/* ============================================================================
 * analysis/LoopHintPass.cpp —— 向后跳转边识别实现
 * ========================================================================== */
#include "analysis/LoopHintPass.h"
#include "cfg/ControlFlowGraph.h"
#include "core/Log.h"

#include <cstdio>

LoopHintPass::LoopHintPass()
    : hintCount_(0)
{
}

bool LoopHintPass::run(ControlFlowGraph *cfg)
{
    hintCount_ = 0;

    if (!validateCFG(cfg, "LoopHintPass")) {
        return false;
    }

    /* 先清除上一次的标记，保证可重复运行 */
    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        b->loopHeader = false;
    }

    /* 遍历所有边，挑出"目标地址 <= 源地址"的那些 */
    for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
        if (!e->isBackwardHint || e->to == NULL) continue;

        e->to->loopHeader = true;      /* 目标块可能是循环头 */
        hintCount_++;
    }

    if (hintCount_ > 0) {
        logInfo("循环提示 : 发现 %lu 条向后跳转边（演示性近似，非严格 back edge）",
                (unsigned long)hintCount_);
    } else {
        logInfo("循环提示 : 未发现向后跳转边");
    }
    return true;
}

void LoopHintPass::printHints(ControlFlowGraph *cfg) const
{
    if (!cfg) return;

    printf("向后跳转边（backward jump hint）:\n");
    size_t shown = 0;
    for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
        if (!e->isBackwardHint || e->to == NULL || e->from == NULL) continue;

        printf("  B%d (0x%llx) -> B%d (0x%llx)",
               e->from->id, (unsigned long long)e->from->startAddress,
               e->to->id,   (unsigned long long)e->to->startAddress);
        if (e->label && e->label[0]) {
            printf("   [%s]", e->label);
        }
        printf("\n");
        shown++;
    }
    if (shown == 0) {
        printf("  （无）\n");
    }
}
