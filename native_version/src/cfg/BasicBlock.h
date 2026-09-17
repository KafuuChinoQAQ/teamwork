/* ============================================================================
 * cfg/BasicBlock.h —— 基本块
 * ----------------------------------------------------------------------------
 * 基本块 = 一段"只有一个入口、一个出口"的连续指令序列。
 * 块内要么全部执行，要么全部不执行；控制流只能从 first 进、从 last 出。
 *
 * 这里保存的是"区间 + 首尾指令指针"两种表示：
 *   - startAddress/endAddress 便于按地址快速定位；
 *   - first/last 指向指令链表中的节点，便于遍历块内指令。
 * ========================================================================== */
#ifndef ELFCFG_CFG_BASICBLOCK_H
#define ELFCFG_CFG_BASICBLOCK_H

#include <cstddef>
#include <cstdint>

/* 这里需要 Instruction 的完整定义：instructionCount() 要沿指令链表遍历 */
#include "arch/Instruction.h"

struct BasicBlock {
    int          id;             /* 基本块编号：B0, B1, B2 …          */
    uint64_t     startAddress;   /* 首指令地址                         */
    uint64_t     endAddress;     /* 末指令**之后**的地址（开区间上界） */
    Instruction *first;          /* 块内第一条指令                     */
    Instruction *last;           /* 块内最后一条指令（决定出边）        */

    /* ---- 分析结果（由各 Pass 填充） ---- */
    bool         reachable;      /* 是否从入口可达（ReachabilityPass）  */
    bool         loopHeader;     /* 是否有向后跳转边指向本块（LoopHintPass）*/

    BasicBlock  *next;           /* 链表指针：下一个基本块             */

    /* 块内指令条数 */
    int instructionCount(void) const
    {
        int n = 0;
        for (const Instruction *ins = first; ins != NULL; ins = ins->next) {
            n++;
            if (ins == last) break;
        }
        return n;
    }

    /* 地址是否落在本块区间内 */
    bool contains(uint64_t address) const
    {
        return address >= startAddress && address < endAddress;
    }

    uint64_t size(void) const { return endAddress - startAddress; }
};

#endif /* ELFCFG_CFG_BASICBLOCK_H */
