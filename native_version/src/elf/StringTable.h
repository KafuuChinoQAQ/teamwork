#ifndef ELFCFG_ELF_STRINGTABLE_H
#define ELFCFG_ELF_STRINGTABLE_H

#include <cstddef>
#include <cstdint>

class StringTable {
public:
    StringTable();

    void bind(const unsigned char *base, size_t size);

    void unbind(void);

    bool   isValid(void) const { return base_ != NULL && size_ > 0; }
    size_t size(void) const    { return size_; }

    const char *get(uint32_t offset) const;

    const char *getOr(uint32_t offset, const char *fallback) const;

    long entryOffset(size_t index) const;

    size_t entryCount(void) const;

    void dump(const char *title) const;

private:
    const unsigned char *base_;
    size_t               size_;

    StringTable(const StringTable &);
    StringTable &operator=(const StringTable &);
};

#endif
