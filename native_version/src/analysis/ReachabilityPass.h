#ifndef ELFCFG_ANALYSIS_REACHABILITYPASS_H
#define ELFCFG_ANALYSIS_REACHABILITYPASS_H

#include <cstddef>

#include "analysis/CFGAnalysisPass.h"

class ReachabilityPass : public CFGAnalysisPass {
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

#endif
