/* EntityList<T>: the level's objects of one kind, owned by the list and kept
 * in the order they were added.  Removing one keeps the order of the rest.
 *
 * Constness is deep: a const list hands out const objects only.  This is what
 * makes a const World a read-only view of the level (the player AI, the HUD,
 * the debug panels).
 *
 * The objects are held by pointer so they stay put while the list changes.
 * During a walk that may remove the current object, use the indices: erase(i)
 * moves the next object down into i. */
#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <vector>

template <class T>
class EntityList {
    using Store = std::vector<std::unique_ptr<T>>;

    /* Turns the store's unique_ptrs into references to E (T or const T). */
    template <class E, class It>
    class Iter {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = E;
        using difference_type   = std::ptrdiff_t;
        using pointer           = E *;
        using reference         = E &;

        Iter() = default;
        explicit Iter(It it) : it_(it) {}
        E    &operator*() const  { return **it_; }
        E    *operator->() const { return it_->get(); }
        Iter &operator++()       { ++it_; return *this; }
        Iter  operator++(int)    { Iter t = *this; ++it_; return t; }
        bool  operator==(const Iter &o) const { return it_ == o.it_; }
        bool  operator!=(const Iter &o) const { return it_ != o.it_; }

    private:
        It it_{};
    };

public:
    using iterator       = Iter<T, typename Store::iterator>;
    using const_iterator = Iter<const T, typename Store::const_iterator>;

    std::size_t size() const  { return objs_.size(); }
    bool        empty() const { return objs_.empty(); }

    T       &operator[](std::size_t i)       { return *objs_[i]; }
    const T &operator[](std::size_t i) const { return *objs_[i]; }

    iterator       begin()       { return iterator(objs_.begin()); }
    iterator       end()         { return iterator(objs_.end()); }
    const_iterator begin() const { return const_iterator(objs_.begin()); }
    const_iterator end() const   { return const_iterator(objs_.end()); }

    /* Takes ownership of obj, appended at the end; returns it. */
    T &add(std::unique_ptr<T> obj)
    {
        objs_.push_back(std::move(obj));
        return *objs_.back();
    }

    /* Destroys the object at i; those after it move down one. */
    void erase(std::size_t i) { objs_.erase(objs_.begin() + (std::ptrdiff_t)i); }

    /* Destroys every object, first to last. */
    void clear()
    {
        for (auto &o : objs_)
            o.reset();
        objs_.clear();
    }

private:
    Store objs_;
};
