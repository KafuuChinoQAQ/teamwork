#include "cfg/BasicBlockBuilder.h"
#include "cfg/ControlFlowGraph.h"
#include "arch/Instruction.h"
#include "core/Log.h"

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
        if (leaders[i] == addr) return;
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

int LinearBasicBlockBuilder::endsBlock(const Instruction *ins) const
{
    if (BasicBlockBuilder::endsBlock(ins)) return 1;
    if (!ins) return 0;
    return (ins->type == INST_RET || ins->type == INST_HLT) ? 1 : 0;
}

bool LinearBasicBlockBuilder::build(ControlFlowGraph *cfg, Instruction *head)
{
    if (!cfg || !head) {
        logError("BasicBlockBuilder: 参数无效");
        return false;
    }

    uint64_t leaders[kMaxLeaders];
    int      nLeaders = 0;

    leaders[nLeaders++] = head->address;
    head->leader = true;

    for (Instruction *ins = head; ins != NULL; ins = ins->next) {
        if (ins->targetKnown &&
            (ins->type == INST_JMP || ins->type == INST_JCC ||
             ins->type == INST_LOOP)) {
            addLeader(leaders, &nLeaders, kMaxLeaders, ins->target);
        }

        if (endsBlock(ins) && ins->next != NULL) {
            addLeader(leaders, &nLeaders, kMaxLeaders, ins->next->address);
        }
    }

    for (Instruction *ins = head; ins != NULL; ins = ins->next) {
        if (isLeader(leaders, nLeaders, ins->address)) {
            ins->leader = true;
        }
    }

    logDebug("BasicBlockBuilder: 共收集到 %d 个基本块入口", nLeaders);

    Instruction *cur   = head;
    BasicBlock  *entry = NULL;

    while (cur != NULL) {
        Instruction *first = cur;
        Instruction *last  = cur;

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

        cur = cur->next;
    }

    cfg->setEntryBlock(entry);

    logInfo("基本块   : 划分出 %lu 个块（%s，Leader 法）",
            (unsigned long)cfg->blockCount(), name());
    return true;
}
