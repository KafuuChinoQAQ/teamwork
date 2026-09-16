/* ============================================================================
 * core/SimpleVector.h —— 教学用动态数组（替代 std::vector）
 * ----------------------------------------------------------------------------
 * 由于项目禁止使用 STL，而后续分析需要动态增长的容器，这里手工实现一个。
 *
 * 设计约束：
 *   - 存储连续内存，容量不足时按 2 倍扩容（realloc）；
 *   - 只支持**可平凡复制**的类型 T（本项目用于 Instruction*、
 *     BasicBlock*、Edge*、int、unsigned long 等），因此内部
 *     直接用赋值/拷贝即可，不需要处理构造析构；
 *   - 不做异常，分配失败返回 false / NULL，由调用方决定如何报错。
 *
 * 边界说明：
 *   - operator[] 不做边界检查（与数组一致，便于教学对照）；
 *     需要安全检查时用 at()，越界返回 NULL。
 * ========================================================================== */
#ifndef ELFCFG_CORE_SIMPLEVECTOR_H
#define ELFCFG_CORE_SIMPLEVECTOR_H

#include <cstddef>
#include <cstdlib>

template <typename T>
class SimpleVector {
public:
    SimpleVector() : data_(NULL), size_(0), capacity_(0) {}

    ~SimpleVector()
    {
        if (data_) {
            free(data_);
            data_ = NULL;
        }
        size_ = 0;
        capacity_ = 0;
    }

    /* 在尾部追加一个元素；容量不足时自动扩容。失败返回 false */
    bool pushBack(const T &value)
    {
        if (size_ >= capacity_ && !grow()) return false;
        data_[size_++] = value;
        return true;
    }

    /* 不检查边界（与原生数组一致） */
    T &operator[](size_t index)             { return data_[index]; }
    const T &operator[](size_t index) const { return data_[index]; }

    /* 带边界检查的访问；越界返回 NULL */
    T *at(size_t index)
    {
        if (index >= size_) return NULL;
        return &data_[index];
    }

    const T *at(size_t index) const
    {
        if (index >= size_) return NULL;
        return &data_[index];
    }

    size_t size(void) const     { return size_; }
    size_t capacity(void) const { return capacity_; }
    bool   empty(void) const    { return size_ == 0; }

    /* 逻辑清空：只把长度归零，不释放内存（便于复用） */
    void clear(void) { size_ = 0; }

    /* 线性查找；找到返回第一个下标，找不到返回 size() */
    size_t indexOf(const T &value) const
    {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i] == value) return i;
        }
        return size_;
    }

    bool contains(const T &value) const
    {
        return indexOf(value) != size_;
    }

private:
    T      *data_;
    size_t  size_;
    size_t  capacity_;

    bool grow(void)
    {
        size_t newCapacity = (capacity_ == 0) ? 16u : capacity_ * 2u;

        /* 溢出检查：newCapacity * sizeof(T) 不能回绕 */
        if (newCapacity < capacity_) return false;
        if (sizeof(T) != 0 && newCapacity > (size_t)-1 / sizeof(T)) return false;

        T *p = (T *)realloc(data_, newCapacity * sizeof(T));
        if (!p) return false;

        data_     = p;
        capacity_ = newCapacity;
        return true;
    }

    SimpleVector(const SimpleVector &);
    SimpleVector &operator=(const SimpleVector &);
};

#endif /* ELFCFG_CORE_SIMPLEVECTOR_H */
