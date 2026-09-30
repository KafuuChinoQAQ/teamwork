#ifndef ELFCFG_CORE_MEMORYARENA_H
#define ELFCFG_CORE_MEMORYARENA_H

#include <cstddef>

class MemoryArena {
public:
    MemoryArena();
    ~MemoryArena();

    void *allocateRaw(size_t bytes);

    template <typename T>
    T *create()
    {
        void *p = allocateRaw(sizeof(T));
        if (!p) return NULL;
        return static_cast<T *>(p);
    }

    size_t totalBytes(void) const;
    size_t liveObjects(void) const;
    size_t chunkCount(void) const;

private:
    struct Chunk {
        Chunk         *next;
        unsigned char *base;
        size_t         capacity;
        size_t         used;
    };

    Chunk *chunks_;
    size_t totalBytes_;
    size_t liveObjects_;

    Chunk *newChunk(size_t minBytes);

    MemoryArena(const MemoryArena &);
    MemoryArena &operator=(const MemoryArena &);
};

#endif
