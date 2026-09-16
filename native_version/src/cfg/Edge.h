/* ============================================================================
 * cfg/Edge.h —— 基本块之间的执行关系（有向边）
 * ----------------------------------------------------------------------------
 * 相比早期版本使用字符串标签（"jle"/"fall"），这里改用**枚举类型**，
 * 好处是分析代码可以直接 switch，不必做字符串比较；同时保留了原始
 * 目标地址与"是否解析成功"，便于处理间接跳转等无法静态确定的情况。
 * ========================================================================== */
#ifndef ELFCFG_CFG_EDGE_H
#define ELFCFG_CFG_EDGE_H

#include <cstdint>

struct BasicBlock;

enum EdgeType {
    EDGE_FALLTHROUGH = 0,   /* 顺序执行到下一块            */
    EDGE_BRANCH_TRUE,       /* 条件跳转：条件成立，跳到目标 */
    EDGE_BRANCH_FALSE,      /* 条件跳转：条件不成立，顺序落下 */
    EDGE_UNCONDITIONAL,     /* 无条件跳转 jmp              */
    EDGE_CALL,              /* 函数调用（本实现不进入被调函数）*/
    EDGE_RETURN,            /* 函数返回                    */
    EDGE_INDIRECT,          /* 间接跳转（目标静态不可知）    */
    EDGE_UNKNOWN
};

const char *edgeTypeName(EdgeType type);

struct Edge {
    BasicBlock *from;            /* 源基本块                    */
    BasicBlock *to;              /* 目标基本块（可能为 NULL）    */
    EdgeType    type;

    uint64_t    targetAddress;   /* 边指向的目标地址             */
    bool        targetResolved;  /* 目标是否成功解析到基本块      */

    /* ---- 向后跳转边的演示性近似 ----
     * 判定方式：to->startAddress <= from->startAddress。
     * 严格意义的 CFG back edge 需要支配关系判定（目标块支配源块），
     * 本实现未做支配树分析，因此这里只作为"循环提示"。 */
    bool        isBackwardHint;

    const char *label;           /* 可选：触发该边的指令助记符 */

    Edge       *next;
};

#endif /* ELFCFG_CFG_EDGE_H */
