/* ============================================================================
 * core/MemoryArena.h —— 集中式内存池
 * ----------------------------------------------------------------------------
 * 分析过程中会产生大量小对象（Instruction / BasicBlock / Edge 等）。
 * 如果每个对象都单独 malloc，会有两个问题：
 *   1. 分配器开销大、内存碎片多；
 *   2. 释放时容易漏掉某条链表，造成泄漏（旧版本就是手工逐个 free 的）。
 *
 * MemoryArena 的做法：
 *   - 按块(chunk)向系统申请内存，块内线性切分；
 *   - 每次切分出来的内存都清零（本项目中对象都是 POD 结构，清零即完成初始化）；
 *   - 析构时一次性归还所有块，因此**不需要**为每个对象写释放代码。
 *
 * 注意：这里只负责"原始内存"，不调用构造函数。
 *       使用 create<T>() 时要求 T 是 POD(平凡可复制、无构造函数)类型，
 *       本项目的 Instruction / BasicBlock / Edge 都满足。
 * ========================================================================== */
#ifndef ELFCFG_CORE_MEMORYARENA_H
#define ELFCFG_CORE_MEMORYARENA_H

#include <cstddef>

class MemoryArena {
public:
    MemoryArena();
    ~MemoryArena();

    /* 分配 bytes 字节并清零。失败返回 NULL（同时记录 ERROR 日志） */
    void *allocateRaw(size_t bytes);

    /* 按类型分配并清零。
     * 返回值可以直接当已初始化对象使用（字段全 0 / NULL）。 */
    template <typename T>
    T *create()
    {
        void *p = allocateRaw(sizeof(T));
        if (!p) return NULL;
        return static_cast<T *>(p);       /* allocateRaw 内部已 memset 清零 */
    }

    /* ---- 统计信息（供 StatisticsPass 与调试使用） ---- */
    size_t totalBytes(void) const;        /* 已分配字节总数（按 8 字节对齐后） */
    size_t liveObjects(void) const;       /* 已分配对象个数 */
    size_t chunkCount(void) const;        /* 当前块数量 */

private:
    struct Chunk {
        Chunk         *next;
        unsigned char *base;              /* malloc 得到的原始指针 */
        size_t         capacity;
        size_t         used;
    };

    Chunk *chunks_;
    size_t totalBytes_;
    size_t liveObjects_;

    /* 申请一个新块，容量至少 minBytes */
    Chunk *newChunk(size_t minBytes);

    MemoryArena(const MemoryArena &);
    MemoryArena &operator=(const MemoryArena &);
};

#endif /* ELFCFG_CORE_MEMORYARENA_H */
