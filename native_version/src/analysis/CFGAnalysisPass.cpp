#include "analysis/CFGAnalysisPass.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "core/Log.h"

bool CFGAnalysisPass::validateCFG(ControlFlowGraph *cfg, const char *passName) const
{
    if (!cfg || cfg->isEmpty()) {
        logError("%s: CFG 为空", passName ? passName : "CFGAnalysisPass");
        return false;
    }
    return true;
}

Edge *CFGAnalysisPass::firstOutEdge(ControlFlowGraph *cfg, const BasicBlock *b)
{
    if (!cfg || !b) return NULL;
    for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
        if (e->from == b) return e;
    }
    return NULL;
}

Edge *CFGAnalysisPass::nextOutEdge(const Edge *e, const BasicBlock *b)
{
    if (!e || !b) return NULL;
    for (Edge *n = e->next; n != NULL; n = n->next) {
        if (n->from == b) return n;
    }
    return NULL;
}

size_t CFGAnalysisPass::countInstructions(ControlFlowGraph *cfg)
{
    if (!cfg) return 0;

    size_t total = 0;
    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        for (Instruction *ins = b->first; ins != NULL; ins = ins->next) {
            total++;
            if (ins == b->last) break;
        }
    }
    return total;
}
