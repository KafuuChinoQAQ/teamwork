/* ============================================================================
 * cfg/EdgeAnalyzer.cpp —— 基本块执行关系识别实现
 * ========================================================================== */
#include "cfg/EdgeAnalyzer.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "core/Log.h"

/* ---------------------------------------------------------------------------
 * 基类：与架构无关的默认规则
 * ------------------------------------------------------------------------- */
void EdgeAnalyzer::analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b)
{
    if (!cfg || !b) return;

    /* 默认策略：顺序执行到下一个基本块 */
    if (b->next) {
        cfg->addEdge(b, b->next, EDGE_FALLTHROUGH,
                     b->next->startAddress, true, "fall");
    }
}

/* ---------------------------------------------------------------------------
 * X86EdgeAnalyzer
 * ------------------------------------------------------------------------- */
bool X86EdgeAnalyzer::analyze(ControlFlowGraph *cfg)
{
    if (!cfg || cfg->isEmpty()) {
        logError("EdgeAnalyzer: CFG 为空，无法分析执行关系");
        return false;
    }

    /* 遍历所有基本块，逐个生成出边（analyzeBlock 是虚函数 —— 多态分发） */
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

    Instruction *last = b->last;      /* 只看块的最后一条指令 */

    switch (last->type) {

    /* ---------------- 条件跳转：两条出边 ---------------- */
    case INST_JCC:
    case INST_LOOP: {
        /* 真分支：跳到目标地址所在的基本块 */
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
        /* 假分支：条件不成立，顺序执行下一块 */
        if (b->next) {
            cfg->addEdge(b, b->next, EDGE_BRANCH_FALSE,
                         b->next->startAddress, true, "fall");
        }
        break;
    }

    /* ---------------- 无条件跳转：一条出边 ---------------- */
    case INST_JMP: {
        if (last->isIndirect) {
            /* 间接跳转：目标来自寄存器/内存，静态不可知 */
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
        /* 无条件跳转之后不再顺序执行 */
        break;
    }

    /* ---------------- 函数返回 / 停机：没有出边 ---------------- */
    case INST_RET:
    case INST_HLT:
        /* 控制流在此终止，CFG 到达函数出口 */
        break;

    /* ---------------- 块尾是 call：返回后继续 ---------------- */
    case INST_CALL:
        if (b->next) {
            cfg->addEdge(b, b->next, EDGE_CALL,
                         b->next->startAddress, true, "ret-to");
        }
        break;

    /* ---------------- 其它指令：顺序执行 ---------------- */
    default:
        if (b->next) {
            cfg->addEdge(b, b->next, EDGE_FALLTHROUGH,
                         b->next->startAddress, true, "fall");
        }
        break;
    }
}
