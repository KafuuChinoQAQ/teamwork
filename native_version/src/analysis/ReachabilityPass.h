/* ============================================================================
 * analysis/ReachabilityPass.h —— 可达性分析
 * ----------------------------------------------------------------------------
 * 从入口基本块出发做图遍历，标记所有可达块。
 * 不可达块通常来自：ret 之后的填充字节、编译器生成的冷代码、
 * 或者被优化掉的死分支 —— 它们对理解程序行为没有意义，可以据此过滤。
 * ========================================================================== */
#ifndef ELFCFG_ANALYSIS_REACHABILITYPASS_H
#define ELFCFG_ANALYSIS_REACHABILITYPASS_H

#include <cstddef>

#include "analysis/AnalysisPass.h"

class ReachabilityPass : public AnalysisPass {
public:
    ReachabilityPass();

    virtual const char *name(void) const { return "reachability"; }
    virtual const char *description(void) const {
        return "从入口块出发标记可达基本块";
    }

    virtual bool run(ControlFlowGraph *cfg);

    size_t reachableCount(void) const   { return reachableCount_; }
    size_t unreachableCount(void) const { return unreachableCount_; }

private:
    size_t reachableCount_;
    size_t unreachableCount_;

    ReachabilityPass(const ReachabilityPass &);
    ReachabilityPass &operator=(const ReachabilityPass &);
};

#endif /* ELFCFG_ANALYSIS_REACHABILITYPASS_H */
