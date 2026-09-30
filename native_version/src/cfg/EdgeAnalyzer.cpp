#include "cfg/EdgeAnalyzer.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "core/Log.h"

void EdgeAnalyzer::analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b)
{
    if (!cfg || !b) return;

    if (b->next) {
        cfg->addEdge(b, b->next, EDGE_FALLTHROUGH,
                     b->next->startAddress, true, "fall");
    }
}

bool X86EdgeAnalyzer::analyze(ControlFlowGraph *cfg)
{
    if (!cfg || cfg->isEmpty()) {
        logError("EdgeAnalyzer: CFG 为空，无法分析执行关系");
        return false;
    }

    for (BasicBlock *b = cfg->blocks(); b != NULL; b = b->next) {
        analyzeBlock(cfg, b);
    }

    logInfo("执行关系 : %lu 条边（其中向后跳转边 %lu 条，%s）",
            (unsigned long)cfg->edgeCount(),
            (unsigned long)cfg->backwardEdgeCount(), name());
    return true;
}

void X86EdgeAnalyzer::analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b)
{
    if (!cfg || !b || !b->last) return;

    Instruction *last = b->last;

    switch (last->type) {

    case INST_JCC:
    case INST_LOOP: {
        if (last->targetKnown) {
            BasicBlock *target = cfg->findBlockByAddress(last->target);
            if (target) {
                cfg->addEdge(b, target, EDGE_BRANCH_TRUE,
                             last->target, true, last->mnemonic);
            } else {
                logWarn("0x%llx 处 %s 的目标 0x%llx 不在函数范围内，未生成该边",
                        (unsigned long long)last->address, last->mnemonic,
                        (unsigned long long)last->target);
            }
        }
        if (b->next) {
            cfg->addEdge(b, b->next, EDGE_BRANCH_FALSE,
                         b->next->startAddress, true, "fall");
        }
        break;
    }

    case INST_JMP: {
        if (last->isIndirect) {
            cfg->addEdge(b, NULL, EDGE_INDIRECT, 0, false, last->mnemonic);
            logInfo("0x%llx 处为间接跳转（%s %s），目标不可静态确定",
                    (unsigned long long)last->address,
                    last->mnemonic, last->operands);
            break;
        }
        if (last->targetKnown) {
            BasicBlock *target = cfg->findBlockByAddress(last->target);
            if (target) {
                cfg->addEdge(b, target, EDGE_UNCONDITIONAL,
                             last->target, true, last->mnemonic);
            } else {
                logWarn("0x%llx 处 jmp 的目标 0x%llx 不在函数范围内，未生成该边",
                        (unsigned long long)last->address,
                        (unsigned long long)last->target);
            }
        }
        break;
    }

    case INST_RET:
    case INST_HLT:
        break;

    case INST_CALL:
        if (b->next) {
            cfg->addEdge(b, b->next, EDGE_CALL,
                         b->next->startAddress, true, "ret-to");
        }
        break;

    default:
        if (b->next) {
            cfg->addEdge(b, b->next, EDGE_FALLTHROUGH,
                         b->next->startAddress, true, "fall");
        }
        break;
    }
}
