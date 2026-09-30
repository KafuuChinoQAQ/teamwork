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

    bool pushBack(const T &value)
    {
        if (size_ >= capacity_ && !grow()) return false;
        data_[size_++] = value;
        return true;
    }

    T &operator[](size_t index)             { return data_[index]; }
    const T &operator[](size_t index) const { return data_[index]; }

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

    void clear(void) { size_ = 0; }

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

#endif
