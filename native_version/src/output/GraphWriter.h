/* ============================================================================
 * output/GraphWriter.h —— 图形/文本输出抽象层
 * ----------------------------------------------------------------------------
 * 输出层只依赖 ControlFlowGraph，不关心它是怎么来的。
 * 这样既可以把 CFG 输出成 Graphviz DOT（交给外部 dot 渲染），
 * 也可以输出成纯文本（便于课堂口头讲解与人眼对照）。
 *
 * 注意：本程序**不依赖 Graphviz 库**，只生成 DOT 文本；
 *       dot 命令仅作为课堂展示时的渲染工具，不参与任何分析。
 * ========================================================================== */
#ifndef ELFCFG_OUTPUT_GRAPHWRITER_H
#define ELFCFG_OUTPUT_GRAPHWRITER_H

#include <cstdio>

class ControlFlowGraph;

class GraphWriter {
public:
    GraphWriter() {}
    virtual ~GraphWriter() {}

    virtual const char *name(void) const = 0;

    /* 把 CFG 写到 out。成功返回 true */
    virtual bool write(FILE *out, ControlFlowGraph *cfg) = 0;

protected:
    /* 按 DOT 语法转义后输出 */
    static void writeEscaped(FILE *out, const char *s);
    /* 输出带引号的 DOT 字符串 */
    static void writeQuoted(FILE *out, const char *s);
    /* 把字符串转义进 dst（只处理 " 和 \），返回写入长度 */
    static int  dotEscapeText(char *dst, int dstSize, const char *src);

    /* 判断块是否为出口块（以 ret/hlt 结尾） */
    static int  isExitBlock(const void *block);

private:
    GraphWriter(const GraphWriter &);
    GraphWriter &operator=(const GraphWriter &);
};

#endif /* ELFCFG_OUTPUT_GRAPHWRITER_H */
