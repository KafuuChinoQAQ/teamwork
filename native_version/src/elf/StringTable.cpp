/* ============================================================================
 * elf/StringTable.cpp —— ELF 字符串表实现
 * ========================================================================== */
#include "elf/StringTable.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

StringTable::StringTable()
    : base_(NULL), size_(0)
{
}

void StringTable::bind(const unsigned char *base, size_t size)
{
    base_ = base;
    size_ = size;

    if (base_ && size_ > 0) {
        logDebug("字符串表: 绑定 %lu 字节，约 %lu 个字符串",
                 (unsigned long)size_, (unsigned long)entryCount());
    }
}

void StringTable::unbind(void)
{
    base_ = NULL;
    size_ = 0;
}

const char *StringTable::get(uint32_t offset) const
{
    if (!base_ || size_ == 0) return NULL;
    if ((size_t)offset >= size_) return NULL;              /* 偏移越界 */

    const char *start = (const char *)(base_ + offset);

    /* 必须能在剩余范围内找到结尾 '\0'，否则视为损坏 */
    if (memchr(start, '\0', size_ - (size_t)offset) == NULL) return NULL;

    return start;
}

const char *StringTable::getOr(uint32_t offset, const char *fallback) const
{
    const char *s = get(offset);
    return s ? s : fallback;
}

long StringTable::entryOffset(size_t index) const
{
    if (!base_ || size_ == 0) return -1;

    size_t   current = 0;      /* 当前字符串起始偏移 */
    size_t   seen    = 0;

    while (current < size_) {
        if (seen == index) return (long)current;
        /* 跳过当前字符串 */
        const char *end = (const char *)memchr(base_ + current, '\0',
                                               size_ - current);
        if (!end) break;
        size_t next = (size_t)(end - (const char *)base_) + 1;
        if (next <= current) break;                        /* 防御死循环 */
        current = next;
        seen++;
    }
    return -1;
}

size_t StringTable::entryCount(void) const
{
    if (!base_ || size_ == 0) return 0;

    size_t count = 0;
    size_t i     = 0;
    while (i < size_) {
        const char *end = (const char *)memchr(base_ + i, '\0', size_ - i);
        if (!end) break;
        size_t next = (size_t)(end - (const char *)base_) + 1;
        if (next <= i) break;
        count++;
        i = next;
    }
    return count;
}

void StringTable::dump(const char *title) const
{
    if (!isValid()) {
        fprintf(stderr, "%s: (未绑定或为空)\n", title ? title : "字符串表");
        return;
    }

    fprintf(stderr, "%s（%lu 字节，共 %lu 个字符串）:\n",
            title ? title : "字符串表",
            (unsigned long)size_, (unsigned long)entryCount());

    size_t i = 0;
    size_t shown = 0;
    while (i < size_ && shown < 64) {          /* 最多打印 64 条，避免刷屏 */
        const char *end = (const char *)memchr(base_ + i, '\0', size_ - i);
        if (!end) break;
        size_t len = (size_t)(end - (const char *)(base_ + i));
        if (len > 0) {
            fprintf(stderr, "  [%04lu] %s\n", (unsigned long)i,
                    (const char *)(base_ + i));
        }
        size_t next = (size_t)(end - (const char *)base_) + 1;
        if (next <= i) break;
        i = next;
        shown++;
    }
    if (i < size_) {
        fprintf(stderr, "  ...（其余字符串已省略）\n");
    }
}
