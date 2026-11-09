/* EntityList<T>: the level's objects of one kind, held by value and kept in
 * the order they were added.  Removing one keeps the order of the rest.
 * Copying the list copies the objects: they hold no pointers, so a copy is a
 * whole, separate level's worth.
 *
 * Constness is deep: a const list hands out const objects only.  This is what
 * makes a const World a read-only view of the level (the player AI, the HUD,
 * the debug panels).
 *
 * The objects move as the list changes: a reference to one lasts until the
 * list next gains or loses one, so none is kept across an add or an erase.
 * During a walk that may remove the current object, use the indices: erase(i)
 * moves the next object down into i.
 *
 * Each object also has an id, given when it is added and never reused until
 * the list is cleared: what names it from outside the list (a sound, an
 * effect, the bot), where its index shifts as others are removed.  A kind
 * kept in a list derives from Listed, which holds it. */
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

template <class T> class EntityList;

/* An object kept in an EntityList: its id there. */
class Listed {
public:
    unsigned id() const { return id_; }

private:
    template <class T> friend class EntityList;
    unsigned id_ = 0;
};

template <class T>
class EntityList {
    using Store = std::vector<T>;

public:
    using iterator       = typename Store::iterator;
    using const_iterator = typename Store::const_iterator;

    std::size_t size() const  { return objs_.size(); }
    bool        empty() const { return objs_.empty(); }

    T       &operator[](std::size_t i)       { return objs_[i]; }
    const T &operator[](std::size_t i) const { return objs_[i]; }

    iterator       begin()       { return objs_.begin(); }
    iterator       end()         { return objs_.end(); }
    const_iterator begin() const { return objs_.begin(); }
    const_iterator end() const   { return objs_.end(); }

    /* Appends obj at the end, giving it the next id; returns it. */
    T &add(T obj)
    {
        obj.id_ = nextId_++;
        objs_.push_back(std::move(obj));
        return objs_.back();
    }

    /* Removes the object at i; those after it move down one. */
    void erase(std::size_t i)
    {
        objs_.erase(objs_.begin() + (std::ptrdiff_t)i);
    }

    /* Removes every object, and starts the ids again. */
    void clear()
    {
        objs_.clear();
        nextId_ = 0;
    }

private:
    Store    objs_;
    unsigned nextId_ = 0;
};
