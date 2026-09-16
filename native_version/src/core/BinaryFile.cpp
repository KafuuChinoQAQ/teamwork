/* ============================================================================
 * core/BinaryFile.cpp —— mmap 实现
 * ----------------------------------------------------------------------------
 * 使用的 Linux/POSIX 接口：
 *      open(2) fstat(2) mmap(2) munmap(2) close(2)
 * ========================================================================== */
#include "core/BinaryFile.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

/* ---------------------------------------------------------------------------
 * MappedBinaryFile
 * ------------------------------------------------------------------------- */
MappedBinaryFile::MappedBinaryFile()
    : fd_(-1), map_(NULL), size_(0), isOpen_(false)
{
    path_[0] = '\0';
}

MappedBinaryFile::~MappedBinaryFile()
{
    closeFile();                          /* 析构时确保资源释放 */
}

bool MappedBinaryFile::openFile(const char *path)
{
    if (isOpen_) {
        logWarn("BinaryFile: 已经打开了 %s，先关闭旧文件", path_);
        closeFile();
    }
    if (!path || !*path) {
        logError("BinaryFile: 文件路径为空");
        return false;
    }
    if (strlen(path) >= sizeof(path_)) {
        logError("BinaryFile: 路径过长（上限 %lu 字节）",
                 (unsigned long)(sizeof(path_) - 1));
        return false;
    }

    /* ---- 1. open ---- */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        logError("BinaryFile: 无法打开 '%s'：%s", path, strerror(errno));
        return false;
    }

    /* ---- 2. fstat：拿到文件大小 ---- */
    struct stat st;
    if (fstat(fd, &st) != 0) {
        logError("BinaryFile: fstat('%s') 失败：%s", path, strerror(errno));
        close(fd);
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        logError("BinaryFile: '%s' 不是普通文件", path);
        close(fd);
        return false;
    }
    if (st.st_size <= 0) {
        logError("BinaryFile: '%s' 是空文件", path);
        close(fd);
        return false;
    }

    size_t fileSize = (size_t)st.st_size;

    /* ---- 3. mmap ---- */
    void *m = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED) {
        logError("BinaryFile: mmap('%s', %lu 字节) 失败：%s",
                 path, (unsigned long)fileSize, strerror(errno));
        close(fd);
        return false;
    }

    /* 提示内核我们将会顺序＋随机混合访问（可选，失败不影响正确性） */
    (void)madvise(m, fileSize, MADV_WILLNEED);

    fd_     = fd;
    map_    = (unsigned char *)m;
    size_   = fileSize;
    isOpen_ = true;
    strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';

    logDebug("BinaryFile: mmap 成功 '%s'，%lu 字节，映射地址 %p",
             path_, (unsigned long)size_, (const void *)map_);
    return true;
}

void MappedBinaryFile::closeFile(void)
{
    if (map_ && size_ > 0) {
        if (munmap(map_, size_) != 0) {
            logWarn("BinaryFile: munmap 失败：%s", strerror(errno));
        }
    }
    if (fd_ >= 0) {
        if (close(fd_) != 0) {
            logWarn("BinaryFile: close 失败：%s", strerror(errno));
        }
    }
    fd_     = -1;
    map_    = NULL;
    size_   = 0;
    isOpen_ = false;
    path_[0] = '\0';
}

const unsigned char *MappedBinaryFile::data(void) const
{
    return map_;
}

size_t MappedBinaryFile::size(void) const
{
    return size_;
}

bool MappedBinaryFile::isOpen(void) const
{
    return isOpen_;
}

const char *MappedBinaryFile::path(void) const
{
    return path_;
}
