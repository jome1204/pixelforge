#pragma once

#include "pixelforge/image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace pixelforge::internal {

inline bool add_overflow(size_t a, size_t b, size_t &result) {
  if (a > std::numeric_limits<size_t>::max() - b)
    return true;
  result = a + b;
  return false;
}

inline bool multiply_overflow(size_t a, size_t b, size_t &result) {
  if (a && b > std::numeric_limits<size_t>::max() / a)
    return true;
  result = a * b;
  return false;
}

inline bool range_valid(size_t offset, size_t length, size_t size) {
  return offset <= size && length <= size - offset;
}

inline uint16_t read_u16(const uint8_t *p, ByteOrder order) {
  if (order == ByteOrder::little)
    return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8);
  return uint16_t(uint16_t(p[0]) << 8) | p[1];
}

inline uint32_t read_u24_be(const uint8_t *p) {
  return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
}

inline uint32_t read_u32(const uint8_t *p, ByteOrder order) {
  if (order == ByteOrder::little)
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
           uint32_t(p[3]) << 24;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
         uint32_t(p[2]) << 8 | p[3];
}

inline uint64_t read_u64(const uint8_t *p, ByteOrder order) {
  if (order == ByteOrder::little)
    return uint64_t(read_u32(p, order)) |
           uint64_t(read_u32(p + 4, order)) << 32;
  return uint64_t(read_u32(p, order)) << 32 | read_u32(p + 4, order);
}

inline int16_t read_i16(const uint8_t *p, ByteOrder order) {
  return static_cast<int16_t>(read_u16(p, order));
}

inline int32_t read_i32(const uint8_t *p, ByteOrder order) {
  return static_cast<int32_t>(read_u32(p, order));
}

inline int64_t read_i64(const uint8_t *p, ByteOrder order) {
  return static_cast<int64_t>(read_u64(p, order));
}

inline void write_u16(std::vector<uint8_t> &out, uint16_t value,
                      ByteOrder order) {
  if (order == ByteOrder::little) {
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
  } else {
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
  }
}

inline void write_u32(std::vector<uint8_t> &out, uint32_t value,
                      ByteOrder order) {
  if (order == ByteOrder::little) {
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 24));
  } else {
    out.push_back(uint8_t(value >> 24));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
  }
}

inline void write_u64(std::vector<uint8_t> &out, uint64_t value,
                      ByteOrder order) {
  if (order == ByteOrder::little) {
    write_u32(out, uint32_t(value), order);
    write_u32(out, uint32_t(value >> 32), order);
  } else {
    write_u32(out, uint32_t(value >> 32), order);
    write_u32(out, uint32_t(value), order);
  }
}

inline bool fail(Error &error, ErrorCode code, size_t offset,
                 std::string message) {
  if (!error)
    error = {code, offset, std::move(message)};
  return false;
}

inline uint16_t expand_to_u16(uint32_t value, unsigned bits) {
  if (!bits)
    return 0;
  if (bits >= 16)
    return uint16_t(value);
  uint32_t maximum = (uint32_t(1) << bits) - 1;
  return uint16_t((uint64_t(value) * 65535 + maximum / 2) / maximum);
}

inline uint8_t u16_to_u8(uint16_t value) {
  return uint8_t((uint32_t(value) + 128) / 257);
}

inline double clamp_unit(double value) {
  return std::max(0.0, std::min(1.0, value));
}

inline bool dimensions_valid(uint32_t width, uint32_t height,
                             const Limits &limits, Error &error) {
  if (!width || !height)
    return fail(error, ErrorCode::invalid_header, 0,
                "image dimensions must be non-zero");
  if (width > limits.max_width || height > limits.max_height)
    return fail(error, ErrorCode::resource_limit, 0,
                "image dimensions exceed configured limits");
  uint64_t pixels = uint64_t(width) * height;
  if (pixels > limits.max_pixels)
    return fail(error, ErrorCode::resource_limit, 0,
                "image pixel count exceeds configured limit");
  return true;
}

inline bool ascii_iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca >= 'A' && ca <= 'Z')
      ca = uint8_t(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z')
      cb = uint8_t(cb + ('a' - 'A'));
    if (ca != cb)
      return false;
  }
  return true;
}

} // namespace pixelforge::internal
