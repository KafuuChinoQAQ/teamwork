#ifndef ELFCFG_ANALYSIS_CFGANALYSISPASS_H
#define ELFCFG_ANALYSIS_CFGANALYSISPASS_H

#include <cstddef>

#include "analysis/AnalysisPass.h"

class ControlFlowGraph;
struct BasicBlock;
struct Edge;

class CFGAnalysisPass : public AnalysisPass {
public:
    CFGAnalysisPass() {}
    virtual ~CFGAnalysisPass() {}

    virtual bool run(ControlFlowGraph *cfg) = 0;

protected:
    bool validateCFG(ControlFlowGraph *cfg, const char *passName) const;

    static Edge *firstOutEdge(ControlFlowGraph *cfg, const BasicBlock *b);
    static Edge *nextOutEdge(const Edge *e, const BasicBlock *b);

    static size_t countInstructions(ControlFlowGraph *cfg);

private:
    CFGAnalysisPass(const CFGAnalysisPass &);
    CFGAnalysisPass &operator=(const CFGAnalysisPass &);
};

#endif
