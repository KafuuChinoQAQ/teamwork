/* ============================================================================
 * core/BinaryFile.h —— 二进制文件访问抽象层
 * ----------------------------------------------------------------------------
 * 本层只负责"把文件变成一块可读内存"，不关心 ELF 语义。
 *
 * 抽象基类 BinaryFile 定义接口；
 * 派生类 MappedBinaryFile 用 Linux/POSIX 接口实现：
 *
 *      open() → fstat() → mmap()  ...  munmap() → close()
 *
 * 之所以用 mmap 而不是 fstream：
 *   1. ELF 分析本质上是对文件内容的随机访问，映射后可以直接按偏移取指针；
 *   2. 避免整块拷贝；
 *   3. 便于后续做越界检查（data() + size() 就是完整可访问范围）。
 * ========================================================================== */
#ifndef ELFCFG_CORE_BINARYFILE_H
#define ELFCFG_CORE_BINARYFILE_H

#include <cstddef>

class BinaryFile {
public:
    BinaryFile() {}
    virtual ~BinaryFile() {}

    /* 打开文件；成功返回 true。失败时内部已记录日志 */
    virtual bool openFile(const char *path) = 0;

    /* 关闭文件，释放映射。可重复调用 */
    virtual void closeFile(void) = 0;

    /* 映射区首地址；未打开时返回 NULL */
    virtual const unsigned char *data(void) const = 0;

    /* 映射区长度（字节）；未打开时返回 0 */
    virtual size_t size(void) const = 0;

    /* 是否已成功打开 */
    virtual bool isOpen(void) const = 0;

    /* 文件路径；未打开时返回空串 */
    virtual const char *path(void) const = 0;

    /* ---- 边界安全访问辅助：任何越界都返回 false，不产生野指针 ---- */
    bool rangeValid(size_t offset, size_t length) const
    {
        if (!isOpen()) return false;
        if (offset > size()) return false;
        if (length > size() - offset) return false;   /* 防止 offset+length 溢出 */
        return true;
    }

    /* 在 [offset, offset+length) 范围内取指针；越界返回 NULL */
    const unsigned char *at(size_t offset, size_t length) const
    {
        if (!rangeValid(offset, length)) return NULL;
        return data() + offset;
    }

private:
    /* 禁止拷贝：文件映射的所有权必须唯一 */
    BinaryFile(const BinaryFile &);
    BinaryFile &operator=(const BinaryFile &);
};

/* ---------------------------------------------------------------------------
 * MappedBinaryFile —— 基于 mmap 的实现
 * ------------------------------------------------------------------------- */
class MappedBinaryFile : public BinaryFile {
public:
    MappedBinaryFile();
    virtual ~MappedBinaryFile();          /* 虚析构：保证通过基类指针 delete 安全 */

    virtual bool openFile(const char *path);
    virtual void closeFile(void);
    virtual const unsigned char *data(void) const;
    virtual size_t size(void) const;
    virtual bool isOpen(void) const;
    virtual const char *path(void) const;

private:
    int            fd_;       /* 文件描述符，-1 表示未打开 */
    unsigned char *map_;      /* mmap 返回的映射首地址   */
    size_t         size_;     /* 文件长度                 */
    char           path_[512];/* 路径副本                 */
    bool           isOpen_;

    MappedBinaryFile(const MappedBinaryFile &);
    MappedBinaryFile &operator=(const MappedBinaryFile &);
};

#endif /* ELFCFG_CORE_BINARYFILE_H */
