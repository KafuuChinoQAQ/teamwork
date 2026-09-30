#ifndef ELFCFG_ANALYSIS_LOOPHINTPASS_H
#define ELFCFG_ANALYSIS_LOOPHINTPASS_H

#include <cstddef>

#include "analysis/CFGAnalysisPass.h"

class ControlFlowGraph;

class LoopHintPass : public CFGAnalysisPass {
public:
    LoopHintPass();

    virtual const char *name(void) const { return "loop-hint"; }
    virtual const char *description(void) const {
        return "识别向后跳转边（backward jump hint，非严格 back edge）";
    }

    virtual bool run(ControlFlowGraph *cfg);

    size_t hintCount(void) const { return hintCount_; }

    void printHints(ControlFlowGraph *cfg) const;

private:
    size_t hintCount_;

    LoopHintPass(const LoopHintPass &);
    LoopHintPass &operator=(const LoopHintPass &);
};

#endif
