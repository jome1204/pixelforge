#include "internal.h"

#include <new>

namespace pixelforge {

size_t PixelFormat::bytes_per_pixel() const {
  if (!byte_aligned() || !channels)
    return 0;
  size_t bytes_per_sample = (bits_per_sample + 7) / 8;
  if (channels > std::numeric_limits<size_t>::max() / bytes_per_sample)
    return 0;
  return size_t(channels) * bytes_per_sample;
}

bool PixelFormat::byte_aligned() const {
  return bits_per_sample != 0 && bits_per_sample % 8 == 0;
}

bool PixelFormat::valid() const {
  if (!channels || channels > 8 || !bits_per_sample || bits_per_sample > 64)
    return false;
  if (sample_type == SampleType::floating_point && bits_per_sample != 16 &&
      bits_per_sample != 32 && bits_per_sample != 64)
    return false;
  switch (model) {
  case ColorModel::grayscale:
    return channels == 1;
  case ColorModel::grayscale_alpha:
    return channels == 2 && alpha != AlphaMode::none;
  case ColorModel::indexed:
    return channels == 1 && alpha == AlphaMode::none;
  case ColorModel::rgb:
  case ColorModel::ycbcr:
  case ColorModel::lab:
    return channels == 3;
  case ColorModel::rgba:
    return channels == 4 && alpha != AlphaMode::none;
  case ColorModel::cmyk:
    return channels == 4 && alpha == AlphaMode::none;
  case ColorModel::unknown:
    return false;
  }
  return false;
}

bool PixelFormat::has_alpha() const { return alpha != AlphaMode::none; }

std::optional<PixelBuffer>
PixelBuffer::allocate(uint32_t width, uint32_t height, PixelFormat format,
                      const Limits &limits, Error &error) {
  if (!internal::dimensions_valid(width, height, limits, error))
    return {};
  if (!format.valid() || !format.byte_aligned())
    return internal::fail(error, ErrorCode::unsupported_feature, 0,
                          "pixel format is invalid or not byte aligned"),
           std::nullopt;
  size_t pixel_bytes = format.bytes_per_pixel();
  size_t stride = 0, total = 0;
  if (!pixel_bytes ||
      internal::multiply_overflow(size_t(width), pixel_bytes, stride) ||
      internal::multiply_overflow(stride, size_t(height), total))
    return internal::fail(error, ErrorCode::integer_overflow, 0,
                          "pixel-buffer size overflow"),
           std::nullopt;
  if (total > limits.max_decoded_bytes)
    return internal::fail(error, ErrorCode::resource_limit, 0,
                          "pixel buffer exceeds configured byte limit"),
           std::nullopt;
  PixelBuffer buffer;
  buffer.width_ = width;
  buffer.height_ = height;
  buffer.stride_ = stride;
  buffer.format_ = format;
  try {
    buffer.bytes_.assign(total, 0);
  } catch (const std::bad_alloc &) {
    internal::fail(error, ErrorCode::allocation_failed, 0,
                   "pixel-buffer allocation failed");
    return {};
  }
  return buffer;
}

std::optional<PixelBuffer>
PixelBuffer::copy_from(ConstPixelSpan source, const Limits &limits,
                       Error &error) {
  auto result =
      allocate(source.width, source.height, source.format, limits, error);
  if (!result)
    return {};
  size_t row_bytes = result->stride_;
  if (!source.data || source.stride < row_bytes ||
      source.size < source.stride * size_t(source.height)) {
    internal::fail(error, ErrorCode::invalid_pixels, 0,
                   "source pixel span is truncated");
    return {};
  }
  for (uint32_t y = 0; y < source.height; ++y)
    std::memcpy(result->row(y), source.data + size_t(y) * source.stride,
                row_bytes);
  return result;
}

uint8_t *PixelBuffer::row(uint32_t y) {
  if (y >= height_)
    return nullptr;
  return bytes_.data() + size_t(y) * stride_;
}

const uint8_t *PixelBuffer::row(uint32_t y) const {
  if (y >= height_)
    return nullptr;
  return bytes_.data() + size_t(y) * stride_;
}

PixelSpan PixelBuffer::span() {
  return {bytes_.data(), bytes_.size(), stride_, width_, height_, format_};
}

ConstPixelSpan PixelBuffer::span() const {
  return {bytes_.data(), bytes_.size(), stride_, width_, height_, format_};
}

void PixelBuffer::clear(uint8_t value) {
  std::fill(bytes_.begin(), bytes_.end(), value);
}

bool Palette::append(PaletteEntry entry, const Limits &limits, Error &error) {
  if (entries_.size() >= limits.max_palette_entries)
    return internal::fail(error, ErrorCode::resource_limit, entries_.size(),
                          "palette entry limit exceeded");
  entries_.push_back(entry);
  return true;
}

bool Palette::set(size_t index, PaletteEntry entry, Error &error) {
  if (index >= entries_.size())
    return internal::fail(error, ErrorCode::invalid_palette, index,
                          "palette index is out of range");
  entries_[index] = entry;
  return true;
}

const PaletteEntry *Palette::at(size_t index) const {
  return index < entries_.size() ? &entries_[index] : nullptr;
}

std::optional<PixelBuffer> Palette::expand(ConstPixelSpan indices,
                                           const Limits &limits,
                                           Error &error) const {
  if (empty())
    return internal::fail(error, ErrorCode::invalid_palette, 0,
                          "cannot expand an empty palette"),
           std::nullopt;
  if (indices.format.model != ColorModel::indexed ||
      indices.format.bits_per_sample != 8 || indices.format.channels != 1)
    return internal::fail(error, ErrorCode::unsupported_feature, 0,
                          "palette expansion requires 8-bit indices"),
           std::nullopt;
  PixelFormat rgba{ColorModel::rgba, SampleType::unsigned_integer, 8, 4,
                   AlphaMode::straight, ByteOrder::little};
  auto output =
      PixelBuffer::allocate(indices.width, indices.height, rgba, limits, error);
  if (!output)
    return {};
  if (!indices.data || indices.stride < indices.width ||
      indices.size < indices.stride * size_t(indices.height))
    return internal::fail(error, ErrorCode::invalid_pixels, 0,
                          "indexed pixel span is truncated"),
           std::nullopt;
  for (uint32_t y = 0; y < indices.height; ++y) {
    const uint8_t *source = indices.data + size_t(y) * indices.stride;
    uint8_t *destination = output->row(y);
    for (uint32_t x = 0; x < indices.width; ++x) {
      const PaletteEntry *entry = at(source[x]);
      if (!entry)
        return internal::fail(error, ErrorCode::invalid_palette,
                              size_t(y) * indices.stride + x,
                              "pixel references missing palette entry"),
               std::nullopt;
      destination[x * 4] = internal::u16_to_u8(entry->red);
      destination[x * 4 + 1] = internal::u16_to_u8(entry->green);
      destination[x * 4 + 2] = internal::u16_to_u8(entry->blue);
      destination[x * 4 + 3] = internal::u16_to_u8(entry->alpha);
    }
  }
  return output;
}

double FrameTiming::seconds() const {
  return denominator ? double(numerator) / denominator : 0.0;
}

ImageFrame *Image::primary_frame() {
  return frames.empty() ? nullptr : &frames.front();
}

const ImageFrame *Image::primary_frame() const {
  return frames.empty() ? nullptr : &frames.front();
}

bool Image::valid(const Limits &limits, Error &error) const {
  if (!internal::dimensions_valid(width, height, limits, error))
    return false;
  if (!format.valid())
    return internal::fail(error, ErrorCode::invalid_pixels, 0,
                          "image pixel format is invalid");
  if (frames.empty())
    return internal::fail(error, ErrorCode::invalid_pixels, 0,
                          "image contains no frames");
  if (frames.size() > limits.max_frames)
    return internal::fail(error, ErrorCode::resource_limit, 0,
                          "image frame limit exceeded");
  for (size_t index = 0; index < frames.size(); ++index) {
    const ImageFrame &frame = frames[index];
    if (!frame.pixels.width() || !frame.pixels.height())
      return internal::fail(error, ErrorCode::invalid_pixels, index,
                            "frame has empty pixel buffer");
    if (frame.x > width || frame.y > height ||
        frame.pixels.width() > width - frame.x ||
        frame.pixels.height() > height - frame.y)
      return internal::fail(error, ErrorCode::invalid_pixels, index,
                            "frame rectangle exceeds image canvas");
    if (frame.pixels.format().model != format.model ||
        frame.pixels.format().bits_per_sample != format.bits_per_sample)
      return internal::fail(error, ErrorCode::invalid_pixels, index,
                            "frame format differs from image format");
  }
  return true;
}

} // namespace pixelforge
