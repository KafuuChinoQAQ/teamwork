/* ============================================================================
 * core/MemoryArena.cpp —— 内存池实现
 * ========================================================================== */
#include "core/MemoryArena.h"
#include "core/Log.h"

#include <cstdlib>
#include <cstring>

/* 默认块大小 64 KiB：足够容纳几万个 Instruction，又不至于浪费 */
static const size_t kDefaultChunkSize = 64u * 1024u;

/* 所有分配统一按 8 字节对齐，保证任何基础类型都能安全访问 */
static const size_t kAlignment = 8u;

static size_t alignUp(size_t value)
{
    return (value + (kAlignment - 1)) & ~(kAlignment - 1);
}

MemoryArena::MemoryArena()
    : chunks_(NULL), totalBytes_(0), liveObjects_(0)
{
}

MemoryArena::~MemoryArena()
{
    Chunk *c = chunks_;
    while (c) {
        Chunk *next = c->next;
        free(c->base);
        free(c);
        c = next;
    }
    chunks_     = NULL;
    totalBytes_ = 0;
    liveObjects_ = 0;
}

MemoryArena::Chunk *MemoryArena::newChunk(size_t minBytes)
{
    size_t capacity = kDefaultChunkSize;
    if (minBytes > capacity) capacity = minBytes;   /* 超大对象单独成块 */

    Chunk *c = (Chunk *)malloc(sizeof(Chunk));
    if (!c) {
        logError("MemoryArena: 无法分配块描述符（%lu 字节）",
                 (unsigned long)sizeof(Chunk));
        return NULL;
    }

    c->base = (unsigned char *)malloc(capacity);
    if (!c->base) {
        logError("MemoryArena: 无法分配 %lu 字节内存", (unsigned long)capacity);
        free(c);
        return NULL;
    }

    c->capacity = capacity;
    c->used     = 0;
    c->next     = chunks_;        /* 新块挂到链表头 */
    chunks_     = c;

    logDebug("MemoryArena: 新建块 %lu 字节（当前共 %lu 块）",
             (unsigned long)capacity, (unsigned long)chunkCount());
    return c;
}

void *MemoryArena::allocateRaw(size_t bytes)
{
    if (bytes == 0) bytes = kAlignment;
    bytes = alignUp(bytes);

    /* 当前块（链表头）放不下就新建一块 */
    Chunk *c = chunks_;
    if (!c || c->used + bytes > c->capacity) {
        c = newChunk(bytes);
        if (!c) return NULL;
    }

    void *p = c->base + c->used;
    c->used += bytes;

    memset(p, 0, bytes);          /* 清零：POD 结构清零即完成初始化 */

    totalBytes_  += bytes;
    liveObjects_ += 1;
    return p;
}

size_t MemoryArena::totalBytes(void) const
{
    return totalBytes_;
}

size_t MemoryArena::liveObjects(void) const
{
    return liveObjects_;
}

size_t MemoryArena::chunkCount(void) const
{
    size_t n = 0;
    for (const Chunk *c = chunks_; c; c = c->next) n++;
    return n;
}
