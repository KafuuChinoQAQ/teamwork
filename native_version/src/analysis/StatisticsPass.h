#ifndef ELFCFG_ANALYSIS_STATISTICSPASS_H
#define ELFCFG_ANALYSIS_STATISTICSPASS_H

#include <cstddef>

#include "analysis/CFGAnalysisPass.h"

class ControlFlowGraph;

class StatisticsPass : public CFGAnalysisPass {
public:
    struct Stats {
        size_t instructions;
        size_t blocks;
        size_t edges;
        size_t conditionalBranches;
        size_t calls;
        size_t returns;
        size_t indirectJumps;
        size_t backwardHints;
        size_t unreachableBlocks;
        size_t maxBlockInstructions;
    };

    StatisticsPass();

    virtual const char *name(void) const { return "statistics"; }
    virtual const char *description(void) const {
        return "函数规模与指令构成统计";
    }

    virtual bool run(ControlFlowGraph *cfg);

    const Stats &stats(void) const { return stats_; }

    void print(void) const;

private:
    Stats stats_;

    StatisticsPass(const StatisticsPass &);
    StatisticsPass &operator=(const StatisticsPass &);
};

#endif
