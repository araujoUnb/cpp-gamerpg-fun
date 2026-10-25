#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

namespace rpg::util {

// Definitions looked up by their `id` member: a vector sorted by id, so a
// lookup is a binary search over contiguous memory and iteration is in id
// order. Built once at load time; lookups happen inside the simulation loop.
template <class T>
class IdTable {
public:
    // False (and nothing inserted) when the id is already present.
    bool add(T item) {
        const auto it = lower(item.id);
        if (it != items_.end() && it->id == item.id) return false;
        items_.insert(it, std::move(item));
        return true;
    }

    const T* find(std::string_view id) const {
        const auto it = lower(id);
        return it != items_.end() && it->id == id ? &*it : nullptr;
    }

    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    std::size_t size() const { return items_.size(); }

private:
    typename std::vector<T>::const_iterator lower(std::string_view id) const {
        return std::lower_bound(items_.begin(), items_.end(), id,
                                [](const T& x, std::string_view k) { return std::string_view(x.id) < k; });
    }

    std::vector<T> items_;
};

}  // namespace rpg::util
