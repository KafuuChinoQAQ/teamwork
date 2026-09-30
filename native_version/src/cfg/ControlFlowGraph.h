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

    void setFunction(const FunctionInfo *fn) { function_ = fn; }
    const FunctionInfo *function(void) const { return function_; }

    BasicBlock *addBlock(uint64_t startAddress, Instruction *first, Instruction *last);
    Edge       *addEdge(BasicBlock *from, BasicBlock *to, EdgeType type,
                        uint64_t targetAddress, bool targetResolved,
                        const char *label);
    void        setEntryBlock(BasicBlock *b) { entry_ = b; }

    BasicBlock       *blocks(void) const     { return blocksHead_; }
    Edge             *edges(void) const      { return edgesHead_;  }
    BasicBlock       *entryBlock(void) const { return entry_;      }
    size_t            blockCount(void) const { return blockCount_; }
    size_t            edgeCount(void) const  { return edgeCount_;  }
    bool              isEmpty(void) const    { return blocksHead_ == NULL; }

    BasicBlock *findBlockByAddress(uint64_t address) const;
    BasicBlock *blockContaining(uint64_t address) const;
    BasicBlock *blockAt(size_t index) const;

    size_t instructionCount(void) const;
    size_t conditionalBranchCount(void) const;
    size_t callCount(void) const;
    size_t returnCount(void) const;
    size_t indirectCount(void) const;
    size_t backwardEdgeCount(void) const;
    size_t unreachableBlockCount(void) const;

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

    SimpleVector<BasicBlock *> blockIndex_;

    ControlFlowGraph(const ControlFlowGraph &);
    ControlFlowGraph &operator=(const ControlFlowGraph &);
};

#endif
