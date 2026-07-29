#include "internal.h"

namespace pixelforge {

size_t TiffEntry::element_size() const {
  switch (type) {
  case TiffType::byte:
  case TiffType::ascii:
  case TiffType::signed_byte:
  case TiffType::undefined:
    return 1;
  case TiffType::short_value:
  case TiffType::signed_short:
    return 2;
  case TiffType::long_value:
  case TiffType::signed_long:
  case TiffType::floating:
  case TiffType::ifd:
    return 4;
  case TiffType::rational:
  case TiffType::signed_rational:
  case TiffType::double_value:
  case TiffType::long8:
  case TiffType::signed_long8:
  case TiffType::ifd8:
    return 8;
  }
  return 0;
}

std::optional<uint64_t>
TiffEntry::unsigned_value(size_t index, ByteOrder order) const {
  size_t bytes = element_size();
  if (!bytes || index >= count || index > value_bytes.size() / bytes ||
      bytes > value_bytes.size() - index * bytes)
    return {};
  const uint8_t *value = value_bytes.data() + index * bytes;
  switch (type) {
  case TiffType::byte:
  case TiffType::undefined:
    return value[0];
  case TiffType::short_value:
    return internal::read_u16(value, order);
  case TiffType::long_value:
  case TiffType::ifd:
    return internal::read_u32(value, order);
  case TiffType::long8:
  case TiffType::ifd8:
    return internal::read_u64(value, order);
  default:
    return {};
  }
}

std::optional<int64_t> TiffEntry::signed_value(size_t index,
                                               ByteOrder order) const {
  size_t bytes = element_size();
  if (!bytes || index >= count || index > value_bytes.size() / bytes ||
      bytes > value_bytes.size() - index * bytes)
    return {};
  const uint8_t *value = value_bytes.data() + index * bytes;
  switch (type) {
  case TiffType::signed_byte:
    return int8_t(value[0]);
  case TiffType::signed_short:
    return internal::read_i16(value, order);
  case TiffType::signed_long:
    return internal::read_i32(value, order);
  case TiffType::signed_long8:
    return internal::read_i64(value, order);
  default:
    if (auto unsigned_result = unsigned_value(index, order)) {
      if (*unsigned_result <= uint64_t(std::numeric_limits<int64_t>::max()))
        return int64_t(*unsigned_result);
    }
    return {};
  }
}

std::optional<Rational>
TiffEntry::rational_value(size_t index, ByteOrder order) const {
  if ((type != TiffType::rational && type != TiffType::signed_rational) ||
      index >= count || index > value_bytes.size() / 8 ||
      8 > value_bytes.size() - index * 8)
    return {};
  const uint8_t *value = value_bytes.data() + index * 8;
  Rational result;
  if (type == TiffType::rational) {
    result.numerator = internal::read_u32(value, order);
    result.denominator = internal::read_u32(value + 4, order);
  } else {
    result.numerator = internal::read_i32(value, order);
    result.denominator = internal::read_i32(value + 4, order);
  }
  return result.valid() ? std::optional<Rational>(result) : std::nullopt;
}

std::string TiffEntry::ascii_value() const {
  if (type != TiffType::ascii)
    return {};
  size_t length = value_bytes.size();
  while (length && value_bytes[length - 1] == 0)
    --length;
  return std::string(reinterpret_cast<const char *>(value_bytes.data()), length);
}

const TiffEntry *TiffDirectory::find(uint16_t tag) const {
  for (const TiffEntry &entry : entries)
    if (entry.tag == tag)
      return &entry;
  return nullptr;
}

std::vector<const TiffEntry *> TiffDirectory::find_all(uint16_t tag) const {
  std::vector<const TiffEntry *> result;
  for (const TiffEntry &entry : entries)
    if (entry.tag == tag)
      result.push_back(&entry);
  return result;
}

TiffDirectoryReader::TiffDirectoryReader(Limits limits) : limits_(limits) {}

bool TiffDirectoryReader::load_value(const uint8_t *data, size_t size,
                                     ByteOrder order, bool big,
                                     TiffEntry &entry, Error &error) const {
  size_t element = entry.element_size();
  if (!element)
    return internal::fail(error, ErrorCode::invalid_tag, entry.source_offset + 2,
                          "TIFF entry has unknown field type");
  if (entry.count > std::numeric_limits<size_t>::max() / element)
    return internal::fail(error, ErrorCode::integer_overflow,
                          entry.source_offset + 4,
                          "TIFF entry byte count overflows address space");
  size_t bytes = size_t(entry.count) * element;
  if (bytes > limits_.max_metadata_bytes)
    return internal::fail(error, ErrorCode::resource_limit,
                          entry.source_offset + 4,
                          "TIFF entry exceeds metadata byte limit");
  size_t inline_size = big ? 8 : 4;
  if (bytes <= inline_size) {
    size_t value_position = entry.source_offset + (big ? 12 : 8);
    if (!internal::range_valid(value_position, bytes, size))
      return internal::fail(error, ErrorCode::truncated, value_position,
                            "inline TIFF entry value is truncated");
    entry.value_bytes.assign(data + value_position,
                             data + value_position + bytes);
    return true;
  }
  if (entry.value_or_offset > size ||
      bytes > size - size_t(entry.value_or_offset))
    return internal::fail(error, ErrorCode::truncated, entry.source_offset,
                          "TIFF entry value points outside input");
  const uint8_t *begin = data + size_t(entry.value_or_offset);
  entry.value_bytes.assign(begin, begin + bytes);
  return true;
}

bool TiffDirectoryReader::parse_directory(
    const uint8_t *data, size_t size, ByteOrder order, bool big, uint64_t offset,
    TiffDirectory &directory, std::set<uint64_t> &visited, size_t &tag_total,
    Error &error, size_t depth) const {
  if (depth > limits_.max_recursion)
    return internal::fail(error, ErrorCode::resource_limit, size_t(offset),
                          "TIFF directory nesting exceeds limit");
  if (!visited.insert(offset).second)
    return internal::fail(error, ErrorCode::invalid_directory, size_t(offset),
                          "TIFF directory offset cycle detected");
  size_t count_size = big ? 8 : 2;
  size_t entry_size = big ? 20 : 12;
  size_t next_size = big ? 8 : 4;
  if (offset > size || !internal::range_valid(size_t(offset), count_size, size))
    return internal::fail(error, ErrorCode::truncated, size_t(offset),
                          "TIFF directory count is truncated");
  uint64_t count = big ? internal::read_u64(data + size_t(offset), order)
                       : internal::read_u16(data + size_t(offset), order);
  if (count > limits_.max_tag_count - tag_total)
    return internal::fail(error, ErrorCode::resource_limit, size_t(offset),
                          "TIFF tag count exceeds configured limit");
  if (count > std::numeric_limits<size_t>::max() / entry_size)
    return internal::fail(error, ErrorCode::integer_overflow, size_t(offset),
                          "TIFF directory size overflows address space");
  size_t entries_offset = size_t(offset) + count_size;
  size_t entries_bytes = size_t(count) * entry_size;
  if (!internal::range_valid(entries_offset, entries_bytes, size) ||
      !internal::range_valid(entries_offset + entries_bytes, next_size, size))
    return internal::fail(error, ErrorCode::truncated, entries_offset,
                          "TIFF directory entries are truncated");
  directory.offset = offset;
  directory.entries.reserve(size_t(count));
  for (size_t index = 0; index < size_t(count); ++index) {
    size_t position = entries_offset + index * entry_size;
    TiffEntry entry;
    entry.source_offset = position;
    entry.tag = internal::read_u16(data + position, order);
    entry.type = static_cast<TiffType>(
        internal::read_u16(data + position + 2, order));
    entry.count = big ? internal::read_u64(data + position + 4, order)
                      : internal::read_u32(data + position + 4, order);
    entry.value_or_offset =
        big ? internal::read_u64(data + position + 12, order)
            : internal::read_u32(data + position + 8, order);
    if (!load_value(data, size, order, big, entry, error))
      return false;
    directory.entries.push_back(std::move(entry));
  }
  tag_total += size_t(count);
  size_t next_position = entries_offset + entries_bytes;
  directory.next_offset = big ? internal::read_u64(data + next_position, order)
                              : internal::read_u32(data + next_position, order);
  static constexpr uint16_t child_tags[] = {330, 34665, 34853, 40965};
  for (uint16_t child_tag : child_tags) {
    for (const TiffEntry *entry : directory.find_all(child_tag)) {
      for (size_t index = 0; index < entry->count; ++index) {
        auto child_offset = entry->unsigned_value(index, order);
        if (!child_offset || !*child_offset)
          continue;
        if (directory.children.size() >= limits_.max_directory_count)
          return internal::fail(error, ErrorCode::resource_limit,
                                entry->source_offset,
                                "TIFF child directory limit exceeded");
        TiffDirectory child;
        if (!parse_directory(data, size, order, big, *child_offset, child,
                             visited, tag_total, error, depth + 1))
          return false;
        directory.children.push_back(std::move(child));
      }
    }
  }
  return true;
}

std::optional<TiffDocument>
TiffDirectoryReader::parse(const uint8_t *data, size_t size,
                           Error &error) const {
  if (!data || size < 8)
    return internal::fail(error, ErrorCode::truncated, size,
                          "TIFF header is truncated"),
           std::nullopt;
  ByteOrder order;
  if (data[0] == 'I' && data[1] == 'I')
    order = ByteOrder::little;
  else if (data[0] == 'M' && data[1] == 'M')
    order = ByteOrder::big;
  else
    return internal::fail(error, ErrorCode::invalid_signature, 0,
                          "TIFF byte-order marker is invalid"),
           std::nullopt;
  uint16_t version = internal::read_u16(data + 2, order);
  bool big = version == 43;
  if (version != 42 && !big)
    return internal::fail(error, ErrorCode::invalid_header, 2,
                          "TIFF version is unsupported"),
           std::nullopt;
  uint64_t first_offset = 0;
  if (big) {
    if (size < 16)
      return internal::fail(error, ErrorCode::truncated, size,
                            "BigTIFF header is truncated"),
             std::nullopt;
    if (internal::read_u16(data + 4, order) != 8 ||
        internal::read_u16(data + 6, order) != 0)
      return internal::fail(error, ErrorCode::invalid_header, 4,
                            "BigTIFF offset-size fields are invalid"),
             std::nullopt;
    first_offset = internal::read_u64(data + 8, order);
  } else {
    first_offset = internal::read_u32(data + 4, order);
  }
  TiffDocument document;
  document.byte_order = order;
  document.big_tiff = big;
  document.first_directory_offset = first_offset;
  std::set<uint64_t> visited;
  size_t tag_total = 0;
  uint64_t current = first_offset;
  while (current) {
    if (document.directories.size() >= limits_.max_directory_count)
      return internal::fail(error, ErrorCode::resource_limit, size_t(current),
                            "TIFF directory count exceeds limit"),
             std::nullopt;
    TiffDirectory directory;
    if (!parse_directory(data, size, order, big, current, directory, visited,
                         tag_total, error, 0))
      return std::nullopt;
    current = directory.next_offset;
    document.directories.push_back(std::move(directory));
  }
  if (document.directories.empty())
    return internal::fail(error, ErrorCode::invalid_directory,
                          size_t(first_offset), "TIFF has no image directory"),
           std::nullopt;
  return document;
}

} // namespace pixelforge
