/* ============================================================================
 * analysis/StatisticsPass.h —— 统计信息
 * ----------------------------------------------------------------------------
 * 汇总函数的规模与指令构成，输出与课堂展示中的"统计"一段对应。
 * 所有数字都来自前几个阶段的真实解析结果，不做任何估算。
 * ========================================================================== */
#ifndef ELFCFG_ANALYSIS_STATISTICSPASS_H
#define ELFCFG_ANALYSIS_STATISTICSPASS_H

#include <cstddef>

#include "analysis/AnalysisPass.h"

class ControlFlowGraph;

class StatisticsPass : public AnalysisPass {
public:
    struct Stats {
        size_t instructions;         /* 指令总数             */
        size_t blocks;               /* 基本块数             */
        size_t edges;                /* 边数                 */
        size_t conditionalBranches;  /* 条件跳转指令数        */
        size_t calls;                /* call 指令数          */
        size_t returns;              /* ret 指令数           */
        size_t indirectJumps;        /* 间接跳转数            */
        size_t backwardHints;        /* 向后跳转边数          */
        size_t unreachableBlocks;    /* 不可达块数            */
        size_t maxBlockInstructions; /* 最大的块内指令数       */
    };

    StatisticsPass();

    virtual const char *name(void) const { return "statistics"; }
    virtual const char *description(void) const {
        return "函数规模与指令构成统计";
    }

    virtual bool run(ControlFlowGraph *cfg);

    const Stats &stats(void) const { return stats_; }

    /* 打印统计表 */
    void print(void) const;

private:
    Stats stats_;

    StatisticsPass(const StatisticsPass &);
    StatisticsPass &operator=(const StatisticsPass &);
};

#endif /* ELFCFG_ANALYSIS_STATISTICSPASS_H */
