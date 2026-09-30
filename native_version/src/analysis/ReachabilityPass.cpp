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

    if (!validateCFG(cfg, "ReachabilityPass")) {
        return false;
    }

    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        b->reachable = false;
    }

    BasicBlock *entry = cfg->entryBlock();
    if (!entry) {
        logError("ReachabilityPass: 未设置入口块");
        return false;
    }

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

        for (Edge *e = firstOutEdge(cfg, b); e != NULL; e = nextOutEdge(e, b)) {
            if (e->to == NULL) continue;
            if (e->to->reachable) continue;

            e->to->reachable = true;
            if (!work.pushBack(e->to)) {
                logWarn("ReachabilityPass: 队列扩容失败，结果可能不完整");
                break;
            }
        }
    }

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
