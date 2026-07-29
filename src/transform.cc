#include "internal.h"

#include <cmath>

namespace pixelforge {

ImageTransform::ImageTransform(Limits limits) : limits_(limits) {}

std::optional<PixelBuffer> ImageTransform::crop(ConstPixelSpan source,
                                                Rectangle rectangle,
                                                Error &error) const {
  if (!rectangle.width || !rectangle.height || rectangle.x > source.width ||
      rectangle.y > source.height ||
      rectangle.width > source.width - rectangle.x ||
      rectangle.height > source.height - rectangle.y)
    return internal::fail(error, ErrorCode::invalid_pixels, 0,
                          "crop rectangle is outside source image"),
           std::nullopt;
  auto output = PixelBuffer::allocate(rectangle.width, rectangle.height,
                                      source.format, limits_, error);
  if (!output)
    return {};
  size_t bytes = size_t(rectangle.width) * source.format.bytes_per_pixel();
  for (uint32_t y = 0; y < rectangle.height; ++y)
    std::memcpy(output->row(y),
                source.data + size_t(y + rectangle.y) * source.stride +
                    size_t(rectangle.x) * source.format.bytes_per_pixel(),
                bytes);
  return output;
}

std::optional<PixelBuffer> ImageTransform::rotate(ConstPixelSpan source,
                                                  Rotation rotation,
                                                  Error &error) const {
  uint32_t width = rotation == Rotation::clockwise_180 ? source.width
                                                       : source.height;
  uint32_t height = rotation == Rotation::clockwise_180 ? source.height
                                                        : source.width;
  auto output =
      PixelBuffer::allocate(width, height, source.format, limits_, error);
  if (!output)
    return {};
  size_t pixel_bytes = source.format.bytes_per_pixel();
  for (uint32_t y = 0; y < source.height; ++y) {
    for (uint32_t x = 0; x < source.width; ++x) {
      uint32_t destination_x = 0, destination_y = 0;
      if (rotation == Rotation::clockwise_90) {
        destination_x = source.height - 1 - y;
        destination_y = x;
      } else if (rotation == Rotation::clockwise_180) {
        destination_x = source.width - 1 - x;
        destination_y = source.height - 1 - y;
      } else {
        destination_x = y;
        destination_y = source.width - 1 - x;
      }
      std::memcpy(output->row(destination_y) + size_t(destination_x) * pixel_bytes,
                  source.data + size_t(y) * source.stride +
                      size_t(x) * pixel_bytes,
                  pixel_bytes);
    }
  }
  return output;
}

std::optional<PixelBuffer>
ImageTransform::flip_horizontal(ConstPixelSpan source, Error &error) const {
  auto output = PixelBuffer::allocate(source.width, source.height, source.format,
                                      limits_, error);
  if (!output)
    return {};
  size_t pixel_bytes = source.format.bytes_per_pixel();
  for (uint32_t y = 0; y < source.height; ++y)
    for (uint32_t x = 0; x < source.width; ++x)
      std::memcpy(output->row(y) + size_t(source.width - 1 - x) * pixel_bytes,
                  source.data + size_t(y) * source.stride +
                      size_t(x) * pixel_bytes,
                  pixel_bytes);
  return output;
}

std::optional<PixelBuffer>
ImageTransform::flip_vertical(ConstPixelSpan source, Error &error) const {
  auto output = PixelBuffer::allocate(source.width, source.height, source.format,
                                      limits_, error);
  if (!output)
    return {};
  size_t row_bytes = size_t(source.width) * source.format.bytes_per_pixel();
  for (uint32_t y = 0; y < source.height; ++y)
    std::memcpy(output->row(source.height - 1 - y),
                source.data + size_t(y) * source.stride, row_bytes);
  return output;
}

std::optional<PixelBuffer>
ImageTransform::resize(ConstPixelSpan source, uint32_t width, uint32_t height,
                       TransformOptions options, Error &error) const {
  if (!source.data || !source.width || !source.height)
    return internal::fail(error, ErrorCode::invalid_pixels, 0,
                          "resize source is empty"),
           std::nullopt;
  if (options.preserve_aspect) {
    double scale = std::min(double(width) / source.width,
                            double(height) / source.height);
    width = std::max(1u, uint32_t(std::floor(source.width * scale + 0.5)));
    height = std::max(1u, uint32_t(std::floor(source.height * scale + 0.5)));
  }
  auto output =
      PixelBuffer::allocate(width, height, source.format, limits_, error);
  if (!output)
    return {};
  size_t pixel_bytes = source.format.bytes_per_pixel();
  if (options.resample == ResampleFilter::nearest) {
    for (uint32_t y = 0; y < height; ++y) {
      uint32_t source_y =
          std::min(source.height - 1, uint32_t(uint64_t(y) * source.height /
                                               std::max(1u, height)));
      for (uint32_t x = 0; x < width; ++x) {
        uint32_t source_x =
            std::min(source.width - 1, uint32_t(uint64_t(x) * source.width /
                                                std::max(1u, width)));
        std::memcpy(output->row(y) + size_t(x) * pixel_bytes,
                    source.data + size_t(source_y) * source.stride +
                        size_t(source_x) * pixel_bytes,
                    pixel_bytes);
      }
    }
    return output;
  }
  if (source.format.sample_type != SampleType::unsigned_integer ||
      source.format.bits_per_sample != 8)
    return internal::fail(error, ErrorCode::unsupported_feature, 0,
                          "filtered resize currently requires 8-bit samples"),
           std::nullopt;
  for (uint32_t y = 0; y < height; ++y) {
    double source_y = (double(y) + 0.5) * source.height / height - 0.5;
    int64_t y0 = std::max<int64_t>(0, int64_t(std::floor(source_y)));
    int64_t y1 = std::min<int64_t>(source.height - 1, y0 + 1);
    double fy = source_y - std::floor(source_y);
    for (uint32_t x = 0; x < width; ++x) {
      double source_x = (double(x) + 0.5) * source.width / width - 0.5;
      int64_t x0 = std::max<int64_t>(0, int64_t(std::floor(source_x)));
      int64_t x1 = std::min<int64_t>(source.width - 1, x0 + 1);
      double fx = source_x - std::floor(source_x);
      for (size_t channel = 0; channel < source.format.channels; ++channel) {
        double top =
            source.data[size_t(y0) * source.stride + size_t(x0) * pixel_bytes +
                        channel] *
                (1 - fx) +
            source.data[size_t(y0) * source.stride + size_t(x1) * pixel_bytes +
                        channel] *
                fx;
        double bottom =
            source.data[size_t(y1) * source.stride + size_t(x0) * pixel_bytes +
                        channel] *
                (1 - fx) +
            source.data[size_t(y1) * source.stride + size_t(x1) * pixel_bytes +
                        channel] *
                fx;
        output->row(y)[size_t(x) * pixel_bytes + channel] =
            uint8_t(std::clamp(std::lround(top * (1 - fy) + bottom * fy), 0l,
                               255l));
      }
    }
  }
  return output;
}

std::optional<PixelBuffer> ImageTransform::orient(ConstPixelSpan source,
                                                  Orientation orientation,
                                                  Error &error) const {
  switch (orientation) {
  case Orientation::top_left:
    return PixelBuffer::copy_from(source, limits_, error);
  case Orientation::top_right:
    return flip_horizontal(source, error);
  case Orientation::bottom_right:
    return rotate(source, Rotation::clockwise_180, error);
  case Orientation::bottom_left:
    return flip_vertical(source, error);
  case Orientation::right_top:
    return rotate(source, Rotation::clockwise_90, error);
  case Orientation::left_bottom:
    return rotate(source, Rotation::clockwise_270, error);
  case Orientation::left_top: {
    auto rotated = rotate(source, Rotation::clockwise_90, error);
    return rotated
               ? flip_horizontal(
                     static_cast<const PixelBuffer &>(*rotated).span(), error)
               : std::nullopt;
  }
  case Orientation::right_bottom: {
    auto rotated = rotate(source, Rotation::clockwise_90, error);
    return rotated
               ? flip_vertical(static_cast<const PixelBuffer &>(*rotated).span(),
                               error)
               : std::nullopt;
  }
  }
  return {};
}

std::optional<PixelBuffer>
ImageTransform::composite(ConstPixelSpan base, ConstPixelSpan overlay,
                          int64_t offset_x, int64_t offset_y, BlendMode mode,
                          Error &error) const {
  if (base.format.model != ColorModel::rgba ||
      overlay.format.model != ColorModel::rgba ||
      base.format.bits_per_sample != 8 ||
      overlay.format.bits_per_sample != 8)
    return internal::fail(error, ErrorCode::unsupported_feature, 0,
                          "compositing requires 8-bit RGBA pixels"),
           std::nullopt;
  auto output = PixelBuffer::copy_from(base, limits_, error);
  if (!output)
    return {};
  for (uint32_t y = 0; y < overlay.height; ++y) {
    int64_t destination_y = offset_y + y;
    if (destination_y < 0 || destination_y >= base.height)
      continue;
    for (uint32_t x = 0; x < overlay.width; ++x) {
      int64_t destination_x = offset_x + x;
      if (destination_x < 0 || destination_x >= base.width)
        continue;
      const uint8_t *source =
          overlay.data + size_t(y) * overlay.stride + size_t(x) * 4;
      uint8_t *destination =
          output->row(uint32_t(destination_y)) + size_t(destination_x) * 4;
      if (mode == BlendMode::source) {
        std::memcpy(destination, source, 4);
        continue;
      }
      double source_alpha = source[3] / 255.0;
      double destination_alpha = destination[3] / 255.0;
      double alpha =
          source_alpha + destination_alpha * (1.0 - source_alpha);
      for (size_t channel = 0; channel < 3; ++channel) {
        double value =
            alpha == 0
                ? 0
                : (source[channel] * source_alpha +
                   destination[channel] * destination_alpha *
                       (1.0 - source_alpha)) /
                      alpha;
        destination[channel] =
            uint8_t(std::clamp(std::lround(value), 0l, 255l));
      }
      destination[3] =
          uint8_t(std::clamp(std::lround(alpha * 255), 0l, 255l));
    }
  }
  return output;
}

} // namespace pixelforge
