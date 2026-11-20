#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Item {
    uint64_t id = 0;
    std::string name;
    int64_t value = 0;
    uint64_t version = 1;
};

enum class UpdateStatus {
    Ok,
    NotFound,
    Conflict
};

struct UpdateResult {
    UpdateStatus status = UpdateStatus::Ok;
    std::optional<Item> item;
};

class Storage {
public:
    Item create(std::string name, int64_t value);
    std::optional<Item> get(uint64_t id) const;
    std::vector<Item> list(std::size_t offset, std::size_t limit) const;

    UpdateResult put(uint64_t id, std::string name, int64_t value, std::optional<uint64_t> expected_version);
    UpdateResult patch(uint64_t id, std::optional<std::string> name, std::optional<int64_t> value, std::optional<uint64_t> expected_version);
    bool remove(uint64_t id);
    std::optional<Item> increment(uint64_t id, int64_t delta);

private:
    mutable std::shared_mutex mtx_;
    std::unordered_map<uint64_t, Item> items_;
    std::atomic<uint64_t> next_id_{1};
};