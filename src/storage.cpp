#include "storage.hpp"

#include <algorithm>
#include <mutex>

Item Storage::create(std::string name, int64_t value) {
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    Item item{id, std::move(name), value, 1};
    std::unique_lock<std::shared_mutex> lock(mtx_);
    items_.emplace(id, item);
    return item;
}

std::optional<Item> Storage::get(uint64_t id) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = items_.find(id);
    if (it == items_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<Item> Storage::list(std::size_t offset, std::size_t limit) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    std::vector<Item> all;
    all.reserve(items_.size());
    for (const auto& [_, item] : items_) {
        all.push_back(item);
    }
    std::sort(all.begin(), all.end(), [](const Item& a, const Item& b) {
        return a.id < b.id;
    });

    if (offset >= all.size() || limit == 0) {
        return {};
    }
    std::size_t end = std::min(all.size(), offset + limit);
    return std::vector<Item>(all.begin() + static_cast<std::ptrdiff_t>(offset),
                             all.begin() + static_cast<std::ptrdiff_t>(end));
}

UpdateResult Storage::put(uint64_t id, std::string name, int64_t value, std::optional<uint64_t> expected_version) {
    std::unique_lock<std::shared_mutex> lock(mtx_);
    auto it = items_.find(id);
    if (it == items_.end()) {
        return {UpdateStatus::NotFound, std::nullopt};
    }
    if (expected_version.has_value() && it->second.version != *expected_version) {
        return {UpdateStatus::Conflict, std::nullopt};
    }
    it->second.name = std::move(name);
    it->second.value = value;
    it->second.version += 1;
    return {UpdateStatus::Ok, it->second};
}

UpdateResult Storage::patch(uint64_t id, std::optional<std::string> name, std::optional<int64_t> value, std::optional<uint64_t> expected_version) {
    std::unique_lock<std::shared_mutex> lock(mtx_);
    auto it = items_.find(id);
    if (it == items_.end()) {
        return {UpdateStatus::NotFound, std::nullopt};
    }
    if (expected_version.has_value() && it->second.version != *expected_version) {
        return {UpdateStatus::Conflict, std::nullopt};
    }
    if (name.has_value()) {
        it->second.name = std::move(*name);
    }
    if (value.has_value()) {
        it->second.value = *value;
    }
    it->second.version += 1;
    return {UpdateStatus::Ok, it->second};
}

bool Storage::remove(uint64_t id) {
    std::unique_lock<std::shared_mutex> lock(mtx_);
    return items_.erase(id) > 0;
}

std::optional<Item> Storage::increment(uint64_t id, int64_t delta) {
    std::unique_lock<std::shared_mutex> lock(mtx_);
    auto it = items_.find(id);
    if (it == items_.end()) {
        return std::nullopt;
    }
    it->second.value += delta;
    it->second.version += 1;
    return it->second;
}