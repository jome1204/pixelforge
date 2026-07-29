#include "internal.h"

#include <sstream>

namespace pixelforge {
namespace {

size_t value_size(const MetadataValue &value) {
  return std::visit(
      [](const auto &item) -> size_t {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::string> ||
                      std::is_same_v<T, std::vector<uint8_t>>)
          return item.size();
        else if constexpr (std::is_same_v<T, std::vector<int64_t>> ||
                           std::is_same_v<T, std::vector<uint64_t>> ||
                           std::is_same_v<T, std::vector<double>>)
          return item.size() * sizeof(typename T::value_type);
        else
          return sizeof(T);
      },
      value);
}

std::string value_text(const MetadataValue &value) {
  return std::visit(
      [](const auto &item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::string>)
          return item;
        else if constexpr (std::is_same_v<T, int64_t> ||
                           std::is_same_v<T, uint64_t> ||
                           std::is_same_v<T, double>)
          return std::to_string(item);
        else if constexpr (std::is_same_v<T, Rational>)
          return std::to_string(item.numerator) + "/" +
                 std::to_string(item.denominator);
        else
          return {};
      },
      value);
}

} // namespace

bool Metadata::add(MetadataEntry entry, const Limits &limits, Error &error) {
  if (entry.key.empty())
    return internal::fail(error, ErrorCode::invalid_metadata,
                          entry.source_offset, "metadata key is empty");
  if (entry.key.size() > limits.max_text_length ||
      entry.namespace_name.size() > limits.max_text_length ||
      entry.language.size() > limits.max_text_length)
    return internal::fail(error, ErrorCode::resource_limit,
                          entry.source_offset, "metadata key is too long");
  size_t item_bytes = entry.key.size() + entry.namespace_name.size() +
                      entry.language.size() + value_size(entry.value);
  if (item_bytes > limits.max_metadata_bytes ||
      byte_size() > limits.max_metadata_bytes - item_bytes)
    return internal::fail(error, ErrorCode::resource_limit,
                          entry.source_offset,
                          "metadata exceeds configured byte limit");
  entries_.push_back(std::move(entry));
  return true;
}

bool Metadata::set(std::string key, MetadataValue value, const Limits &limits,
                   Error &error) {
  for (MetadataEntry &entry : entries_) {
    if (entry.key == key) {
      size_t old_size = value_size(entry.value);
      size_t new_size = value_size(value);
      size_t current = byte_size();
      if (new_size > old_size &&
          new_size - old_size > limits.max_metadata_bytes - current)
        return internal::fail(error, ErrorCode::resource_limit, 0,
                              "metadata exceeds configured byte limit");
      entry.value = std::move(value);
      return true;
    }
  }
  return add({std::move(key), std::move(value), {}, {}, false, 0}, limits,
             error);
}

const MetadataEntry *Metadata::find(std::string_view key) const {
  for (const MetadataEntry &entry : entries_)
    if (entry.key == key)
      return &entry;
  return nullptr;
}

std::vector<const MetadataEntry *>
Metadata::find_all(std::string_view key) const {
  std::vector<const MetadataEntry *> result;
  for (const MetadataEntry &entry : entries_)
    if (entry.key == key)
      result.push_back(&entry);
  return result;
}

bool Metadata::erase(std::string_view key) {
  size_t before = entries_.size();
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [key](const MetadataEntry &entry) {
                       return entry.key == key;
                     }),
      entries_.end());
  return entries_.size() != before;
}

void Metadata::clear() { entries_.clear(); }

size_t Metadata::byte_size() const {
  size_t total = 0;
  for (const MetadataEntry &entry : entries_) {
    size_t item = entry.key.size() + entry.namespace_name.size() +
                  entry.language.size() + value_size(entry.value);
    if (item > std::numeric_limits<size_t>::max() - total)
      return std::numeric_limits<size_t>::max();
    total += item;
  }
  return total;
}

std::string Metadata::text(std::string_view key, std::string fallback) const {
  const MetadataEntry *entry = find(key);
  return entry ? value_text(entry->value) : std::move(fallback);
}

std::optional<int64_t> Metadata::integer(std::string_view key) const {
  const MetadataEntry *entry = find(key);
  if (!entry)
    return {};
  if (auto value = std::get_if<int64_t>(&entry->value))
    return *value;
  if (auto value = std::get_if<uint64_t>(&entry->value)) {
    if (*value <= uint64_t(std::numeric_limits<int64_t>::max()))
      return int64_t(*value);
  }
  return {};
}

} // namespace pixelforge
