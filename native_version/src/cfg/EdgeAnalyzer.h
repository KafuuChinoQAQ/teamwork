/* ============================================================================
 * cfg/EdgeAnalyzer.h —— 基本块执行关系识别
 * ----------------------------------------------------------------------------
 * 关键观察：一个基本块的**全部出边，只由它的最后一条指令决定**。
 *
 * 基类给出与架构无关的默认规则（顺序执行）；
 * X86EdgeAnalyzer 覆盖 analyzeBlock()，加入 x86 的跳转语义：
 *
 *   条件跳转 Jcc / LOOP  → 2 条边：跳转目标(TRUE) + 顺序下一块(FALSE)
 *   无条件跳转 JMP       → 1 条边：跳转目标
 *   间接跳转 FF /4       → 1 条边：目标不可解析（EDGE_INDIRECT）
 *   ret / hlt            → 0 条边：控制流终止
 *   call 位于块尾        → 1 条边：返回后继续（EDGE_CALL）
 *   其它                 → 1 条边：顺序执行（EDGE_FALLTHROUGH）
 * ========================================================================== */
#ifndef ELFCFG_CFG_EDGEANALYZER_H
#define ELFCFG_CFG_EDGEANALYZER_H

class ControlFlowGraph;
struct BasicBlock;

class EdgeAnalyzer {
public:
    EdgeAnalyzer() {}
    virtual ~EdgeAnalyzer() {}

    virtual const char *name(void) const = 0;

    /* 分析全部基本块，把边写入 cfg；成功返回 true */
    virtual bool analyze(ControlFlowGraph *cfg) = 0;

protected:
    /* 【虚函数】为一个基本块生成出边。
     * 基类实现：只连接顺序执行的下一块 —— 与具体指令集无关。 */
    virtual void analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b);

private:
    EdgeAnalyzer(const EdgeAnalyzer &);
    EdgeAnalyzer &operator=(const EdgeAnalyzer &);
};

/* ---------------------------------------------------------------------------
 * X86EdgeAnalyzer —— 按 x86 指令语义确定出边
 * ------------------------------------------------------------------------- */
class X86EdgeAnalyzer : public EdgeAnalyzer {
public:
    X86EdgeAnalyzer() {}
    virtual ~X86EdgeAnalyzer() {}

    virtual const char *name(void) const { return "x86-cfg"; }
    virtual bool analyze(ControlFlowGraph *cfg);

protected:
    virtual void analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b);
};

#endif /* ELFCFG_CFG_EDGEANALYZER_H */
