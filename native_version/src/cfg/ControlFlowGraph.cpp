/* ============================================================================
 * cfg/ControlFlowGraph.cpp —— 控制流图实现
 * ========================================================================== */
#include "cfg/ControlFlowGraph.h"
#include "core/MemoryArena.h"
#include "core/Log.h"
#include "arch/Instruction.h"
#include "elf/FunctionInfo.h"

#include <cstdio>
#include <cstring>

/* ---------------------------------------------------------------------------
 * 边类型名字
 * ------------------------------------------------------------------------- */
const char *edgeTypeName(EdgeType type)
{
    switch (type) {
    case EDGE_FALLTHROUGH:  return "FALLTHROUGH";
    case EDGE_BRANCH_TRUE:  return "BRANCH_TRUE";
    case EDGE_BRANCH_FALSE: return "BRANCH_FALSE";
    case EDGE_UNCONDITIONAL:return "UNCONDITIONAL";
    case EDGE_CALL:         return "CALL";
    case EDGE_RETURN:       return "RETURN";
    case EDGE_INDIRECT:     return "INDIRECT";
    default:                return "UNKNOWN";
    }
}

/* ---------------------------------------------------------------------------
 * 构造 / 析构
 * ------------------------------------------------------------------------- */
ControlFlowGraph::ControlFlowGraph(MemoryArena *arena)
    : arena_(arena),
      function_(NULL),
      blocksHead_(NULL), blocksTail_(NULL),
      edgesHead_(NULL), edgesTail_(NULL),
      entry_(NULL),
      blockCount_(0), edgeCount_(0), nextBlockId_(0)
{
}

ControlFlowGraph::~ControlFlowGraph()
{
    /* 块与边都由 MemoryArena 统一释放，这里不需要逐个 free */
}

/* ---------------------------------------------------------------------------
 * 构建
 * ------------------------------------------------------------------------- */
BasicBlock *ControlFlowGraph::addBlock(uint64_t startAddress,
                                       Instruction *first,
                                       Instruction *last)
{
    if (!arena_) {
        logError("CFG: 内存池未初始化");
        return NULL;
    }

    BasicBlock *b = arena_->create<BasicBlock>();
    if (!b) {
        logError("CFG: 分配基本块失败");
        return NULL;
    }

    b->id           = nextBlockId_++;
    b->startAddress = startAddress;
    b->endAddress   = (last != NULL) ? last->endAddress() : startAddress;
    b->first        = first;
    b->last         = last;
    b->reachable    = false;
    b->loopHeader   = false;
    b->next         = NULL;

    if (blocksTail_) blocksTail_->next = b;
    else             blocksHead_ = b;
    blocksTail_ = b;
    blockCount_++;

    if (!blockIndex_.pushBack(b)) {
        logWarn("CFG: 块索引追加失败（块数 %lu）", (unsigned long)blockCount_);
    }
    return b;
}

Edge *ControlFlowGraph::addEdge(BasicBlock *from, BasicBlock *to,
                                EdgeType type, uint64_t targetAddress,
                                bool targetResolved, const char *label)
{
    if (!arena_) return NULL;

    Edge *e = arena_->create<Edge>();
    if (!e) {
        logError("CFG: 分配边失败");
        return NULL;
    }

    e->from           = from;
    e->to             = to;
    e->type           = type;
    e->targetAddress  = targetAddress;
    e->targetResolved = targetResolved;
    e->label          = label ? label : "";

    /* 向后跳转边（演示性近似）：目标块地址 <= 源块地址 */
    e->isBackwardHint = (to != NULL && from != NULL &&
                         to->startAddress <= from->startAddress);

    e->next = NULL;
    if (edgesTail_) edgesTail_->next = e;
    else            edgesHead_ = e;
    edgesTail_ = e;
    edgeCount_++;

    return e;
}

/* ---------------------------------------------------------------------------
 * 查找
 * ------------------------------------------------------------------------- */
BasicBlock *ControlFlowGraph::findBlockByAddress(uint64_t address) const
{
    for (BasicBlock *b = blocksHead_; b != NULL; b = b->next) {
        if (b->startAddress == address) return b;
    }
    return NULL;
}

BasicBlock *ControlFlowGraph::blockContaining(uint64_t address) const
{
    for (BasicBlock *b = blocksHead_; b != NULL; b = b->next) {
        if (b->contains(address)) return b;
    }
    return NULL;
}

BasicBlock *ControlFlowGraph::blockAt(size_t index) const
{
    if (index >= blockIndex_.size()) return NULL;
    return blockIndex_[index];
}

/* ---------------------------------------------------------------------------
 * 统计
 * ------------------------------------------------------------------------- */
size_t ControlFlowGraph::instructionCount(void) const
{
    size_t n = 0;
    for (BasicBlock *b = blocksHead_; b != NULL; b = b->next) {
        n += (size_t)b->instructionCount();
    }
    return n;
}

size_t ControlFlowGraph::conditionalBranchCount(void) const
{
    size_t n = 0;
    for (Edge *e = edgesHead_; e != NULL; e = e->next) {
        if (e->type == EDGE_BRANCH_TRUE || e->type == EDGE_BRANCH_FALSE) n++;
    }
    return n;
}

size_t ControlFlowGraph::callCount(void) const
{
    size_t n = 0;
    for (Edge *e = edgesHead_; e != NULL; e = e->next) {
        if (e->type == EDGE_CALL) n++;
    }
    return n;
}

size_t ControlFlowGraph::returnCount(void) const
{
    size_t n = 0;
    for (Edge *e = edgesHead_; e != NULL; e = e->next) {
        if (e->type == EDGE_RETURN) n++;
    }
    return n;
}

size_t ControlFlowGraph::indirectCount(void) const
{
    size_t n = 0;
    for (Edge *e = edgesHead_; e != NULL; e = e->next) {
        if (e->type == EDGE_INDIRECT) n++;
    }
    return n;
}

size_t ControlFlowGraph::backwardEdgeCount(void) const
{
    size_t n = 0;
    for (Edge *e = edgesHead_; e != NULL; e = e->next) {
        if (e->isBackwardHint) n++;
    }
    return n;
}

size_t ControlFlowGraph::unreachableBlockCount(void) const
{
    size_t n = 0;
    for (BasicBlock *b = blocksHead_; b != NULL; b = b->next) {
        if (!b->reachable) n++;
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * 打印
 * ------------------------------------------------------------------------- */
void ControlFlowGraph::printSummary(void) const
{
    printf("CFG 摘要:\n");

    if (function_) {
        printf("  函数      : %s\n", function_->name);
        printf("  函数地址  : 0x%llx\n",
               (unsigned long long)function_->virtualAddress);
        printf("  函数大小  : %llu 字节\n",
               (unsigned long long)function_->size);
    }
    printf("  基本块    : %lu\n", (unsigned long)blockCount_);
    printf("  边        : %lu\n", (unsigned long)edgeCount_);
    printf("  指令      : %lu\n", (unsigned long)instructionCount());
    printf("  条件分支边: %lu\n", (unsigned long)conditionalBranchCount());
    printf("  call      : %lu\n", (unsigned long)callCount());
    printf("  ret       : %lu\n", (unsigned long)returnCount());
    printf("  间接跳转  : %lu\n", (unsigned long)indirectCount());

    if (entry_) {
        printf("  入口块    : B%d（0x%llx）\n", entry_->id,
               (unsigned long long)entry_->startAddress);
    } else {
        printf("  入口块    : (未设置)\n");
    }
}

void ControlFlowGraph::printBlocks(void) const
{
    printf("基本块列表:\n");
    printf("  %-4s %-14s %-14s %-6s %-8s %s\n",
           "ID", "StartAddress", "EndAddress", "指令", "可达", "块尾指令");

    for (BasicBlock *b = blocksHead_; b != NULL; b = b->next) {
        char tail[256];
        if (b->last) {
            snprintf(tail, sizeof(tail), "%s %s",
                     b->last->mnemonic, b->last->operands);
        } else {
            snprintf(tail, sizeof(tail), "(空)");
        }
        printf("  B%-3d 0x%-12llx 0x%-12llx %-6d %-8s %s\n",
               b->id,
               (unsigned long long)b->startAddress,
               (unsigned long long)b->endAddress,
               b->instructionCount(),
               b->reachable ? "是" : "否",
               tail);
    }
}
