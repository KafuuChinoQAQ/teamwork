/* ============================================================================
 * analysis/CFGAnalysisPass.h —— 作用于控制流图的分析 Pass（抽象中间层）
 * ----------------------------------------------------------------------------
 * 继承层次中的中间抽象层，表达"以控制流图为分析对象的一族 Pass"：
 *
 *     AnalysisPass        任意分析（不限定输入形态）
 *            △
 *     CFGAnalysisPass     CFG 上的分析（本文件）—— 本族共享的输入校验与遍历
 *            △
 *     ReachabilityPass / LoopHintPass / StatisticsPass   具体算法
 *
 * 为什么需要这一层：
 *   1. **输入契约**：CFG 类 Pass 都要求"CFG 非空"这一前提，
 *      校验与报错格式统一在本层，不必在每个 Pass 里各写一遍；
 *   2. **遍历模式**：本项目的边存放在一张全局链表里，要取"某个块的出边"
 *      必须按 from 过滤 —— 这个过滤逻辑在本层收口，
 *      避免每个 Pass 各写一套（写错就是漏边或重复计数）；
 *   3. **为后续扩展留出位置**：将来若加入不基于 CFG 的分析
 *      （例如只依赖符号表的导入表分析），它直接继承 AnalysisPass，
 *      与本族并列，而不会被误当成 CFG 分析。
 *
 * 本层不实现任何具体算法，因此是抽象的。
 * ========================================================================== */
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

    /* 本族的分析对象固定为 CFG —— 签名在这里固定下来 */
    virtual bool run(ControlFlowGraph *cfg) = 0;

protected:
    /* ------------------------------------------------------------------
     * 通用输入校验
     * ------------------------------------------------------------------
     * CFG 非空才可分析。失败时输出 "<passName>: CFG 为空" 并返回 false，
     * 各 Pass 保证报错格式一致。
     */
    bool validateCFG(ControlFlowGraph *cfg, const char *passName) const;

    /* ------------------------------------------------------------------
     * 出边遍历辅助
     * ------------------------------------------------------------------
     * 边存放在全局链表里，取"某个块的出边"需要按 from 过滤。
     * 用法：
     *     for (Edge *e = firstOutEdge(cfg, b); e != NULL;
     *          e = nextOutEdge(e, b)) { ... }
     * 语义与手写 `for (e = cfg->edges(); e; e = e->next) if (e->from != b) continue;`
     * 完全一致，只是把过滤收口到一处。
     */
    static Edge *firstOutEdge(ControlFlowGraph *cfg, const BasicBlock *b);
    static Edge *nextOutEdge(const Edge *e, const BasicBlock *b);

    /* ------------------------------------------------------------------
     * 通用统计
     * ------------------------------------------------------------------
     * 沿块内指令链表累计指令数（StatisticsPass 使用；
     * 其它 Pass 需要时也可以直接用，不必各自遍历一遍）。
     */
    static size_t countInstructions(ControlFlowGraph *cfg);

private:
    CFGAnalysisPass(const CFGAnalysisPass &);
    CFGAnalysisPass &operator=(const CFGAnalysisPass &);
};

#endif /* ELFCFG_ANALYSIS_CFGANALYSISPASS_H */
