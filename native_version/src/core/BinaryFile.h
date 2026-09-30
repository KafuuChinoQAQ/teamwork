#ifndef ELFCFG_CORE_BINARYFILE_H
#define ELFCFG_CORE_BINARYFILE_H

#include <cstddef>

class BinaryFile {
public:
    BinaryFile() {}
    virtual ~BinaryFile() {}

    virtual bool openFile(const char *path) = 0;

    virtual void closeFile(void) = 0;

    virtual const unsigned char *data(void) const = 0;

    virtual size_t size(void) const = 0;

    virtual bool isOpen(void) const = 0;

    virtual const char *path(void) const = 0;

    bool rangeValid(size_t offset, size_t length) const
    {
        if (!isOpen()) return false;
        if (offset > size()) return false;
        if (length > size() - offset) return false;
        return true;
    }

    const unsigned char *at(size_t offset, size_t length) const
    {
        if (!rangeValid(offset, length)) return NULL;
        return data() + offset;
    }

private:
    BinaryFile(const BinaryFile &);
    BinaryFile &operator=(const BinaryFile &);
};

class MappedBinaryFile : public BinaryFile {
public:
    MappedBinaryFile();
    virtual ~MappedBinaryFile();

    virtual bool openFile(const char *path);
    virtual void closeFile(void);
    virtual const unsigned char *data(void) const;
    virtual size_t size(void) const;
    virtual bool isOpen(void) const;
    virtual const char *path(void) const;

private:
    int            fd_;
    unsigned char *map_;
    size_t         size_;
    char           path_[512];
    bool           isOpen_;

    MappedBinaryFile(const MappedBinaryFile &);
    MappedBinaryFile &operator=(const MappedBinaryFile &);
};

#endif
