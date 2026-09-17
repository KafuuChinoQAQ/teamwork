/* ============================================================================
 * cfg/BasicBlockBuilder.cpp —— 基本块划分实现
 * ========================================================================== */
#include "cfg/BasicBlockBuilder.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "core/Log.h"

/* ---------------------------------------------------------------------------
 * BasicBlockBuilder 基类
 * ------------------------------------------------------------------------- */
int BasicBlockBuilder::endsBlock(const Instruction *ins) const
{
    if (!ins) return 0;
    return (ins->type == INST_JMP ||
            ins->type == INST_JCC ||
            ins->type == INST_LOOP) ? 1 : 0;
}

void BasicBlockBuilder::addLeader(uint64_t *leaders, int *count,
                                  int maxCount, uint64_t addr)
{
    for (int i = 0; i < *count; ++i) {
        if (leaders[i] == addr) return;          /* 已登记，去重 */
    }
    if (*count < maxCount) {
        leaders[(*count)++] = addr;
    } else {
        logWarn("BasicBlockBuilder: leader 表已满（%d），地址 0x%llx 被丢弃",
                maxCount, (unsigned long long)addr);
    }
}

int BasicBlockBuilder::isLeader(const uint64_t *leaders, int count, uint64_t addr)
{
    for (int i = 0; i < count; ++i) {
        if (leaders[i] == addr) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * LinearBasicBlockBuilder
 * ------------------------------------------------------------------------- */
int LinearBasicBlockBuilder::endsBlock(const Instruction *ins) const
{
    if (BasicBlockBuilder::endsBlock(ins)) return 1;    /* 复用基类规则 */
    if (!ins) return 0;
    return (ins->type == INST_RET || ins->type == INST_HLT) ? 1 : 0;
}

bool LinearBasicBlockBuilder::build(ControlFlowGraph *cfg, Instruction *head)
{
    if (!cfg || !head) {
        logError("BasicBlockBuilder: 参数无效");
        return false;
    }

    /* ================= 第一步：按 Leader 规则收集入口地址 ================= */
    uint64_t leaders[kMaxLeaders];
    int      nLeaders = 0;

    leaders[nLeaders++] = head->address;                 /* 规则1：函数首指令 */
    head->leader = true;

    for (Instruction *ins = head; ins != NULL; ins = ins->next) {
        /* 规则2：跳转目标 */
        if (ins->targetKnown &&
            (ins->type == INST_JMP || ins->type == INST_JCC ||
             ins->type == INST_LOOP)) {
            addLeader(leaders, &nLeaders, kMaxLeaders, ins->target);
        }

        /* 规则3：跳转/终止指令的下一条
         * （对 ret/hlt 也另起一块，避免把不可达代码并进上一块） */
        if (endsBlock(ins) && ins->next != NULL) {
            addLeader(leaders, &nLeaders, kMaxLeaders, ins->next->address);
        }
    }

    /* 把 leader 标记写回指令，方便调试与后续分析 */
    for (Instruction *ins = head; ins != NULL; ins = ins->next) {
        if (isLeader(leaders, nLeaders, ins->address)) {
            ins->leader = true;
        }
    }

    logDebug("BasicBlockBuilder: 共收集到 %d 个基本块入口", nLeaders);

    /* ================= 第二步：沿指令链表切分基本块 ================= */
    Instruction *cur   = head;
    BasicBlock  *entry = NULL;

    while (cur != NULL) {
        Instruction *first = cur;
        Instruction *last  = cur;

        /* 一直向后吃，直到：
         *   (a) 当前指令会终止基本块，或
         *   (b) 下一条指令本身是 leader（新块开头） */
        while (cur->next != NULL
               && !endsBlock(cur)
               && !isLeader(leaders, nLeaders, cur->next->address)) {
            cur  = cur->next;
            last = cur;
        }

        BasicBlock *b = cfg->addBlock(first->address, first, last);
        if (!b) {
            logError("BasicBlockBuilder: 创建基本块失败");
            return false;
        }
        if (!entry) entry = b;

        cur = cur->next;        /* 从下一个入口继续 */
    }

    cfg->setEntryBlock(entry);

    logInfo("基本块   : 划分出 %lu 个块（%s，Leader 法）",
            (unsigned long)cfg->blockCount(), name());
    return true;
}
