/* ============================================================================
 * cfg/ControlFlowGraph.h —— 控制流图
 * ----------------------------------------------------------------------------
 * 这是 CFG 层的"中心对象"：统一持有基本块链表与边链表，并提供按地址查找、
 * 统计、打印等操作。各分析 Pass 都以它为输入。
 *
 * 设计说明：
 *   - 块与边的对象统一由 MemoryArena 分配，CFG 只持有指针；
 *   - 另用 SimpleVector 维护"按加入顺序的块索引"，支持 blockAt(i) 随机访问；
 *   - 不依赖任何 STL 容器。
 * ========================================================================== */
#ifndef ELFCFG_CFG_CONTROLFLOWGRAPH_H
#define ELFCFG_CFG_CONTROLFLOWGRAPH_H

#include <cstddef>
#include <cstdint>

#include "core/SimpleVector.h"
#include "cfg/BasicBlock.h"
#include "cfg/Edge.h"

class MemoryArena;
struct FunctionInfo;
struct Instruction;

class ControlFlowGraph {
public:
    explicit ControlFlowGraph(MemoryArena *arena);
    ~ControlFlowGraph();

    /* ---- 关联的函数信息 ---- */
    void setFunction(const FunctionInfo *fn) { function_ = fn; }
    const FunctionInfo *function(void) const { return function_; }

    /* ---- 构建期接口（由 Builder / Analyzer 调用） ----
     * 块与边都由 arena 分配，失败返回 NULL */
    BasicBlock *addBlock(uint64_t startAddress, Instruction *first, Instruction *last);
    Edge       *addEdge(BasicBlock *from, BasicBlock *to, EdgeType type,
                        uint64_t targetAddress, bool targetResolved,
                        const char *label);
    void        setEntryBlock(BasicBlock *b) { entry_ = b; }

    /* ---- 访问 ---- */
    BasicBlock       *blocks(void) const     { return blocksHead_; }
    Edge             *edges(void) const      { return edgesHead_;  }
    BasicBlock       *entryBlock(void) const { return entry_;      }
    size_t            blockCount(void) const { return blockCount_; }
    size_t            edgeCount(void) const  { return edgeCount_;  }
    bool              isEmpty(void) const    { return blocksHead_ == NULL; }

    /* ---- 查找 ---- */
    BasicBlock *findBlockByAddress(uint64_t address) const;  /* 入口地址精确匹配 */
    BasicBlock *blockContaining(uint64_t address) const;     /* 地址落在块区间内 */
    BasicBlock *blockAt(size_t index) const;                 /* 第 index 个块    */

    /* ---- 统计 ---- */
    size_t instructionCount(void) const;
    size_t conditionalBranchCount(void) const;
    size_t callCount(void) const;
    size_t returnCount(void) const;
    size_t indirectCount(void) const;
    size_t backwardEdgeCount(void) const;
    size_t unreachableBlockCount(void) const;

    /* ---- 打印（供 --summary） ---- */
    void printSummary(void) const;
    void printBlocks(void) const;

    MemoryArena *arena(void) const { return arena_; }

private:
    MemoryArena        *arena_;
    const FunctionInfo *function_;

    BasicBlock *blocksHead_;
    BasicBlock *blocksTail_;
    Edge       *edgesHead_;
    Edge       *edgesTail_;
    BasicBlock *entry_;

    size_t      blockCount_;
    size_t      edgeCount_;
    int         nextBlockId_;

    SimpleVector<BasicBlock *> blockIndex_;   /* 按加入顺序索引 */

    ControlFlowGraph(const ControlFlowGraph &);
    ControlFlowGraph &operator=(const ControlFlowGraph &);
};

#endif /* ELFCFG_CFG_CONTROLFLOWGRAPH_H */
