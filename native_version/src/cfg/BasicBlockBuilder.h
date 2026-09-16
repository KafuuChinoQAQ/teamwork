/* ============================================================================
 * cfg/BasicBlockBuilder.h —— 基本块划分（抽象基类 + Leader 法实现）
 * ----------------------------------------------------------------------------
 * Leader（入口）法三条规则：
 *   规则1：函数的第一条指令是一个基本块的入口；
 *   规则2：跳转指令的目标地址是一个基本块的入口；
 *   规则3：跳转指令之后的下一条指令也是一个基本块的入口。
 * 派生类额外规定：ret/hlt 这类终止指令之后也另起一块，避免把不可达代码
 * 混进上一个基本块。
 *
 * 划分算法与旧版本一致，只是接入方式改为写入 ControlFlowGraph。
 * ========================================================================== */
#ifndef ELFCFG_CFG_BASICBLOCKBUILDER_H
#define ELFCFG_CFG_BASICBLOCKBUILDER_H

#include <cstdint>

class ControlFlowGraph;
struct Instruction;

class BasicBlockBuilder {
public:
    BasicBlockBuilder() {}
    virtual ~BasicBlockBuilder() {}

    virtual const char *name(void) const = 0;

    /* 把指令链表划分成基本块并写入 cfg；成功返回 true */
    virtual bool build(ControlFlowGraph *cfg, Instruction *head) = 0;

protected:
    /* 【虚函数】判断一条指令是否会"终止当前基本块"
     * 基类默认实现：跳转类指令会终止基本块 */
    virtual int endsBlock(const Instruction *ins) const;

    /* 收集 leader 的工具函数 */
    static void addLeader(uint64_t *leaders, int *count, int maxCount, uint64_t addr);
    static int  isLeader(const uint64_t *leaders, int count, uint64_t addr);

    /* Leader 表容量上限（远超实际需要，仅作防御） */
    static const int kMaxLeaders = 8192;

private:
    BasicBlockBuilder(const BasicBlockBuilder &);
    BasicBlockBuilder &operator=(const BasicBlockBuilder &);
};

/* ---------------------------------------------------------------------------
 * LinearBasicBlockBuilder —— 线性扫描 + Leader 法
 * ------------------------------------------------------------------------- */
class LinearBasicBlockBuilder : public BasicBlockBuilder {
public:
    LinearBasicBlockBuilder() {}
    virtual ~LinearBasicBlockBuilder() {}

    virtual const char *name(void) const { return "linear-leader"; }
    virtual bool build(ControlFlowGraph *cfg, Instruction *head);

protected:
    /* 覆盖：x86 上除跳转外，ret/hlt 也终止基本块 */
    virtual int endsBlock(const Instruction *ins) const;
};

#endif /* ELFCFG_CFG_BASICBLOCKBUILDER_H */
