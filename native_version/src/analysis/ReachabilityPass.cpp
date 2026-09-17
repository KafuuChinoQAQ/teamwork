/* ============================================================================
 * analysis/ReachabilityPass.cpp —— 可达性分析实现
 * ----------------------------------------------------------------------------
 * 使用广度优先遍历。工作队列用自制的 SimpleVector 实现（下标推进，
 * 不做 pop，避免额外的数据搬移），不使用 STL。
 * ========================================================================== */
#include "analysis/ReachabilityPass.h"
#include "cfg/ControlFlowGraph.h"
#include "core/SimpleVector.h"
#include "core/Log.h"

#include <cstdio>

ReachabilityPass::ReachabilityPass()
    : reachableCount_(0), unreachableCount_(0)
{
}

bool ReachabilityPass::run(ControlFlowGraph *cfg)
{
    reachableCount_   = 0;
    unreachableCount_ = 0;

    if (!cfg || cfg->isEmpty()) {
        logError("ReachabilityPass: CFG 为空");
        return false;
    }

    /* ---- 1. 先把所有块标记为不可达 ---- */
    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        b->reachable = false;
    }

    BasicBlock *entry = cfg->entryBlock();
    if (!entry) {
        logError("ReachabilityPass: 未设置入口块");
        return false;
    }

    /* ---- 2. 从入口块开始广度优先遍历 ---- */
    SimpleVector<BasicBlock *> work;
    if (!work.pushBack(entry)) {
        logError("ReachabilityPass: 工作队列初始化失败");
        return false;
    }
    entry->reachable = true;

    size_t head = 0;
    while (head < work.size()) {
        BasicBlock *b = work[head];
        head++;

        /* 扫描所有以 b 为起点的边 */
        for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
            if (e->from != b) continue;
            if (e->to == NULL) continue;              /* 间接跳转等未解析的边 */
            if (e->to->reachable) continue;

            e->to->reachable = true;
            if (!work.pushBack(e->to)) {
                logWarn("ReachabilityPass: 队列扩容失败，结果可能不完整");
                break;
            }
        }
    }

    /* ---- 3. 统计 ---- */
    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        if (b->reachable) reachableCount_++;
        else              unreachableCount_++;
    }

    logInfo("可达性   : %lu/%lu 个基本块可达%s",
            (unsigned long)reachableCount_,
            (unsigned long)(reachableCount_ + unreachableCount_),
            unreachableCount_ ? "（存在不可达块）" : "");

    if (unreachableCount_ > 0) {
        for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
            if (!b->reachable) {
                logInfo("           不可达块 B%d（0x%llx）", b->id,
                        (unsigned long long)b->startAddress);
            }
        }
    }
    return true;
}
