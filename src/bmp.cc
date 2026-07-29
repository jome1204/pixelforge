#include "internal.h"

#include <cmath>

namespace pixelforge {
namespace {

constexpr uint32_t bmp_rgb = 0;
constexpr uint32_t bmp_rle8 = 1;
constexpr uint32_t bmp_rle4 = 2;
constexpr uint32_t bmp_bitfields = 3;
constexpr uint32_t bmp_alpha_bitfields = 6;

unsigned trailing_zeroes(uint32_t mask) {
  if (!mask)
    return 0;
  unsigned count = 0;
  while (!(mask & 1)) {
    mask >>= 1;
    ++count;
  }
  return count;
}

unsigned mask_width(uint32_t mask) {
  mask >>= trailing_zeroes(mask);
  unsigned count = 0;
  while (mask & 1) {
    ++count;
    mask >>= 1;
  }
  return count;
}

uint8_t extract_mask(uint32_t pixel, uint32_t mask, uint8_t fallback) {
  if (!mask)
    return fallback;
  unsigned shift = trailing_zeroes(mask);
  unsigned width = mask_width(mask);
  if (!width)
    return fallback;
  uint32_t value = (pixel & mask) >> shift;
  uint64_t maximum = width == 32 ? 0xffffffffu : (uint64_t(1) << width) - 1;
  return uint8_t((uint64_t(value) * 255 + maximum / 2) / maximum);
}

bool set_index(PixelBuffer &buffer, uint32_t x, uint32_t y, uint8_t value,
               Error &error, size_t offset) {
  if (x >= buffer.width() || y >= buffer.height())
    return internal::fail(error, ErrorCode::invalid_pixels, offset,
                          "RLE command writes beyond image bounds");
  buffer.row(y)[x] = value;
  return true;
}

} // namespace

uint32_t BmpHeader::absolute_height() const {
  if (height == std::numeric_limits<int32_t>::min())
    return 0;
  return uint32_t(height < 0 ? -height : height);
}

BmpDecoder::BmpDecoder(Limits limits) : limits_(limits) {}

std::optional<BmpHeader>
BmpDecoder::parse_header(const uint8_t *data, size_t size, Error &error) const {
  if (!data || size < 14)
    return internal::fail(error, ErrorCode::truncated, size,
                          "BMP file header is truncated"),
           std::nullopt;
  if (data[0] != 'B' || data[1] != 'M')
    return internal::fail(error, ErrorCode::invalid_signature, 0,
                          "BMP signature is missing"),
           std::nullopt;
  BmpHeader header;
  header.file_size = internal::read_u32(data + 2, ByteOrder::little);
  header.pixel_offset = internal::read_u32(data + 10, ByteOrder::little);
  if (size < 18)
    return internal::fail(error, ErrorCode::truncated, size,
                          "BMP DIB size is truncated"),
           std::nullopt;
  header.dib_size = internal::read_u32(data + 14, ByteOrder::little);
  if (header.dib_size == 12) {
    if (size < 26)
      return internal::fail(error, ErrorCode::truncated, size,
                            "BITMAPCOREHEADER is truncated"),
             std::nullopt;
    header.width = internal::read_u16(data + 18, ByteOrder::little);
    header.height = internal::read_u16(data + 20, ByteOrder::little);
    header.planes = internal::read_u16(data + 22, ByteOrder::little);
    header.bits_per_pixel = internal::read_u16(data + 24, ByteOrder::little);
  } else if (header.dib_size >= 40) {
    if (header.dib_size > size - 14 || size < 54)
      return internal::fail(error, ErrorCode::truncated, 14,
                            "BMP DIB header extends beyond input"),
             std::nullopt;
    header.width = internal::read_i32(data + 18, ByteOrder::little);
    header.height = internal::read_i32(data + 22, ByteOrder::little);
    header.planes = internal::read_u16(data + 26, ByteOrder::little);
    header.bits_per_pixel = internal::read_u16(data + 28, ByteOrder::little);
    header.compression = internal::read_u32(data + 30, ByteOrder::little);
    header.image_size = internal::read_u32(data + 34, ByteOrder::little);
    header.x_pixels_per_meter =
        internal::read_i32(data + 38, ByteOrder::little);
    header.y_pixels_per_meter =
        internal::read_i32(data + 42, ByteOrder::little);
    header.colors_used = internal::read_u32(data + 46, ByteOrder::little);
    header.important_colors =
        internal::read_u32(data + 50, ByteOrder::little);
    if (header.dib_size >= 52) {
      header.red_mask = internal::read_u32(data + 54, ByteOrder::little);
      header.green_mask = internal::read_u32(data + 58, ByteOrder::little);
      header.blue_mask = internal::read_u32(data + 62, ByteOrder::little);
    }
    if (header.dib_size >= 56)
      header.alpha_mask = internal::read_u32(data + 66, ByteOrder::little);
    if ((header.compression == bmp_bitfields ||
         header.compression == bmp_alpha_bitfields) &&
        header.dib_size == 40) {
      size_t mask_offset = 14 + header.dib_size;
      size_t mask_count = header.compression == bmp_alpha_bitfields ? 4 : 3;
      if (!internal::range_valid(mask_offset, mask_count * 4, size))
        return internal::fail(error, ErrorCode::truncated, mask_offset,
                              "BMP bit masks are truncated"),
               std::nullopt;
      header.red_mask =
          internal::read_u32(data + mask_offset, ByteOrder::little);
      header.green_mask =
          internal::read_u32(data + mask_offset + 4, ByteOrder::little);
      header.blue_mask =
          internal::read_u32(data + mask_offset + 8, ByteOrder::little);
      if (mask_count == 4)
        header.alpha_mask =
            internal::read_u32(data + mask_offset + 12, ByteOrder::little);
    }
  } else {
    return internal::fail(error, ErrorCode::unsupported_feature, 14,
                          "unsupported BMP DIB header size"),
           std::nullopt;
  }
  if (header.width <= 0 || !header.absolute_height())
    return internal::fail(error, ErrorCode::invalid_header, 18,
                          "BMP dimensions are invalid"),
           std::nullopt;
  if (!internal::dimensions_valid(uint32_t(header.width),
                                  header.absolute_height(), limits_, error))
    return std::nullopt;
  if (header.planes != 1)
    return internal::fail(error, ErrorCode::invalid_header, 26,
                          "BMP plane count must be one"),
           std::nullopt;
  switch (header.bits_per_pixel) {
  case 1:
  case 4:
  case 8:
  case 16:
  case 24:
  case 32:
    break;
  default:
    return internal::fail(error, ErrorCode::unsupported_feature, 28,
                          "unsupported BMP bit depth"),
           std::nullopt;
  }
  if (header.pixel_offset > size)
    return internal::fail(error, ErrorCode::truncated, 10,
                          "BMP pixel offset exceeds input"),
           std::nullopt;
  if (header.compression != bmp_rgb && header.compression != bmp_rle8 &&
      header.compression != bmp_rle4 &&
      header.compression != bmp_bitfields &&
      header.compression != bmp_alpha_bitfields)
    return internal::fail(error, ErrorCode::unsupported_feature, 30,
                          "unsupported BMP compression"),
           std::nullopt;
  if ((header.compression == bmp_rle8 && header.bits_per_pixel != 8) ||
      (header.compression == bmp_rle4 && header.bits_per_pixel != 4))
    return internal::fail(error, ErrorCode::invalid_header, 30,
                          "BMP RLE mode does not match bit depth"),
           std::nullopt;
  return header;
}

bool BmpDecoder::decode_uncompressed(const uint8_t *data, size_t size,
                                     const BmpHeader &header,
                                     const Palette &palette,
                                     PixelBuffer &output,
                                     Error &error) const {
  uint64_t row_bits = uint64_t(uint32_t(header.width)) * header.bits_per_pixel;
  uint64_t disk_stride64 = ((row_bits + 31) / 32) * 4;
  if (disk_stride64 > std::numeric_limits<size_t>::max())
    return internal::fail(error, ErrorCode::integer_overflow, 0,
                          "BMP row stride overflows address space");
  size_t disk_stride = size_t(disk_stride64);
  size_t required = 0;
  if (internal::multiply_overflow(disk_stride, header.absolute_height(),
                                  required) ||
      !internal::range_valid(header.pixel_offset, required, size))
    return internal::fail(error, ErrorCode::truncated, header.pixel_offset,
                          "BMP pixel array is truncated");
  for (uint32_t source_y = 0; source_y < header.absolute_height(); ++source_y) {
    uint32_t y = header.top_down() ? source_y
                                   : header.absolute_height() - 1 - source_y;
    const uint8_t *source =
        data + header.pixel_offset + size_t(source_y) * disk_stride;
    uint8_t *destination = output.row(y);
    for (uint32_t x = 0; x < uint32_t(header.width); ++x) {
      if (header.bits_per_pixel <= 8) {
        uint8_t index = 0;
        if (header.bits_per_pixel == 8)
          index = source[x];
        else if (header.bits_per_pixel == 4)
          index = uint8_t((source[x / 2] >> (x % 2 ? 0 : 4)) & 15);
        else
          index = uint8_t((source[x / 8] >> (7 - x % 8)) & 1);
        const PaletteEntry *entry = palette.at(index);
        if (!entry)
          return internal::fail(error, ErrorCode::invalid_palette,
                                header.pixel_offset +
                                    size_t(source_y) * disk_stride + x,
                                "BMP pixel references missing palette entry");
        destination[x * 4] = internal::u16_to_u8(entry->red);
        destination[x * 4 + 1] = internal::u16_to_u8(entry->green);
        destination[x * 4 + 2] = internal::u16_to_u8(entry->blue);
        destination[x * 4 + 3] = internal::u16_to_u8(entry->alpha);
      } else if (header.bits_per_pixel == 24) {
        destination[x * 4] = source[x * 3 + 2];
        destination[x * 4 + 1] = source[x * 3 + 1];
        destination[x * 4 + 2] = source[x * 3];
        destination[x * 4 + 3] = 255;
      } else {
        size_t sample_bytes = header.bits_per_pixel / 8;
        uint32_t value = sample_bytes == 2
                             ? internal::read_u16(source + x * sample_bytes,
                                                  ByteOrder::little)
                             : internal::read_u32(source + x * sample_bytes,
                                                  ByteOrder::little);
        uint32_t red_mask = header.red_mask;
        uint32_t green_mask = header.green_mask;
        uint32_t blue_mask = header.blue_mask;
        if (!red_mask && header.bits_per_pixel == 16) {
          red_mask = 0x7c00;
          green_mask = 0x03e0;
          blue_mask = 0x001f;
        } else if (!red_mask) {
          red_mask = 0x00ff0000;
          green_mask = 0x0000ff00;
          blue_mask = 0x000000ff;
        }
        destination[x * 4] = extract_mask(value, red_mask, 0);
        destination[x * 4 + 1] = extract_mask(value, green_mask, 0);
        destination[x * 4 + 2] = extract_mask(value, blue_mask, 0);
        destination[x * 4 + 3] =
            extract_mask(value, header.alpha_mask, 255);
      }
    }
  }
  return true;
}

bool BmpDecoder::decode_rle(const uint8_t *data, size_t size,
                            const BmpHeader &header, PixelBuffer &output,
                            Error &error) const {
  size_t cursor = header.pixel_offset;
  uint32_t x = 0;
  uint32_t source_y = 0;
  bool ended = false;
  while (cursor < size && !ended) {
    if (!internal::range_valid(cursor, 2, size))
      return internal::fail(error, ErrorCode::truncated, cursor,
                            "BMP RLE command is truncated");
    uint8_t count = data[cursor++];
    uint8_t value = data[cursor++];
    uint32_t y = header.top_down() ? source_y
                                   : header.absolute_height() - 1 - source_y;
    if (count) {
      for (uint32_t i = 0; i < count; ++i) {
        uint8_t index = header.bits_per_pixel == 8
                            ? value
                            : uint8_t((value >> (i % 2 ? 0 : 4)) & 15);
        if (!set_index(output, x++, y, index, error, cursor - 2))
          return false;
      }
      continue;
    }
    if (value == 0) {
      x = 0;
      if (++source_y > header.absolute_height())
        return internal::fail(error, ErrorCode::invalid_pixels, cursor - 2,
                              "BMP RLE row count exceeds image height");
    } else if (value == 1) {
      ended = true;
    } else if (value == 2) {
      if (!internal::range_valid(cursor, 2, size))
        return internal::fail(error, ErrorCode::truncated, cursor,
                              "BMP RLE delta is truncated");
      x += data[cursor++];
      source_y += data[cursor++];
      if (x > uint32_t(header.width) ||
          source_y >= header.absolute_height())
        return internal::fail(error, ErrorCode::invalid_pixels, cursor - 2,
                              "BMP RLE delta leaves image bounds");
    } else {
      uint32_t literal_pixels = value;
      size_t literal_bytes =
          header.bits_per_pixel == 8 ? literal_pixels : (literal_pixels + 1) / 2;
      if (!internal::range_valid(cursor, literal_bytes, size))
        return internal::fail(error, ErrorCode::truncated, cursor,
                              "BMP RLE literal command is truncated");
      for (uint32_t i = 0; i < literal_pixels; ++i) {
        uint8_t index =
            header.bits_per_pixel == 8
                ? data[cursor + i]
                : uint8_t((data[cursor + i / 2] >> (i % 2 ? 0 : 4)) & 15);
        if (!set_index(output, x++, y, index, error, cursor + i))
          return false;
      }
      cursor += literal_bytes;
      if (literal_bytes & 1) {
        if (cursor >= size)
          return internal::fail(error, ErrorCode::truncated, cursor,
                                "BMP RLE padding byte is missing");
        ++cursor;
      }
    }
  }
  if (!ended)
    return internal::fail(error, ErrorCode::truncated, cursor,
                          "BMP RLE end marker is missing");
  return true;
}

DecodeResult BmpDecoder::decode(const uint8_t *data, size_t size,
                                DecodeOptions options) const {
  DecodeResult result;
  if (size > limits_.max_input_bytes) {
    result.error = {ErrorCode::resource_limit, 0,
                    "BMP input exceeds configured byte limit"};
    return result;
  }
  auto header = parse_header(data, size, result.error);
  if (!header)
    return result;
  Palette palette;
  if (header->bits_per_pixel <= 8) {
    uint32_t maximum = uint32_t(1) << header->bits_per_pixel;
    uint32_t count = header->colors_used ? header->colors_used : maximum;
    if (!count || count > maximum || count > limits_.max_palette_entries) {
      result.error = {ErrorCode::invalid_palette, 46,
                      "BMP palette size is invalid"};
      return result;
    }
    size_t entry_size = header->dib_size == 12 ? 3 : 4;
    size_t palette_offset = 14 + header->dib_size;
    if (header->dib_size == 40 &&
        (header->compression == bmp_bitfields ||
         header->compression == bmp_alpha_bitfields))
      palette_offset += header->compression == bmp_alpha_bitfields ? 16 : 12;
    size_t palette_bytes = 0;
    if (internal::multiply_overflow(count, entry_size, palette_bytes) ||
        !internal::range_valid(palette_offset, palette_bytes, size) ||
        palette_offset + palette_bytes > header->pixel_offset) {
      result.error = {ErrorCode::truncated, palette_offset,
                      "BMP palette extends beyond pixel data"};
      return result;
    }
    for (uint32_t index = 0; index < count; ++index) {
      const uint8_t *entry = data + palette_offset + index * entry_size;
      Error error;
      if (!palette.append({uint16_t(entry[2] * 257u),
                           uint16_t(entry[1] * 257u),
                           uint16_t(entry[0] * 257u), 65535},
                          limits_, error)) {
        result.error = error;
        return result;
      }
    }
  }
  Image image;
  image.width = uint32_t(header->width);
  image.height = header->absolute_height();
  image.format = {ColorModel::rgba, SampleType::unsigned_integer, 8, 4,
                  AlphaMode::straight, ByteOrder::little};
  image.palette = std::move(palette);
  if (options.decode_metadata) {
    Error ignored;
    image.metadata.set("bmp.x_pixels_per_meter",
                       int64_t(header->x_pixels_per_meter), limits_, ignored);
    image.metadata.set("bmp.y_pixels_per_meter",
                       int64_t(header->y_pixels_per_meter), limits_, ignored);
  }
  if (options.decode_pixels) {
    ImageFrame frame;
    if (header->compression == bmp_rle4 || header->compression == bmp_rle8) {
      PixelFormat indexed{ColorModel::indexed, SampleType::unsigned_integer, 8,
                          1, AlphaMode::none, ByteOrder::little};
      auto indices = PixelBuffer::allocate(image.width, image.height, indexed,
                                           limits_, result.error);
      if (!indices ||
          !decode_rle(data, size, *header, *indices, result.error))
        return result;
      auto expanded = image.palette.expand(
          static_cast<const PixelBuffer &>(*indices).span(), limits_,
          result.error);
      if (!expanded)
        return result;
      frame.pixels = std::move(*expanded);
    } else {
      auto pixels = PixelBuffer::allocate(image.width, image.height,
                                          image.format, limits_, result.error);
      if (!pixels ||
          !decode_uncompressed(data, size, *header, image.palette, *pixels,
                               result.error))
        return result;
      frame.pixels = std::move(*pixels);
    }
    image.frames.push_back(std::move(frame));
  } else {
    PixelFormat format = image.format;
    auto placeholder =
        PixelBuffer::allocate(1, 1, format, limits_, result.error);
    if (!placeholder)
      return result;
    image.frames.push_back({std::move(*placeholder)});
    image.frames.back().x = 0;
    image.frames.back().y = 0;
  }
  result.image = std::move(image);
  return result;
}

BmpEncoder::BmpEncoder(Limits limits) : limits_(limits) {}

EncodeResult BmpEncoder::encode(const Image &image, EncodeOptions options) const {
  EncodeResult result;
  Error validation;
  if (!image.valid(limits_, validation)) {
    result.error = std::move(validation);
    return result;
  }
  const ImageFrame *frame = image.primary_frame();
  if (!frame) {
    result.error = {ErrorCode::invalid_pixels, 0, "image has no primary frame"};
    return result;
  }
  ColorConverter converter(limits_);
  PixelFormat rgba{ColorModel::rgba, SampleType::unsigned_integer, 8, 4,
                   AlphaMode::straight, ByteOrder::little};
  Error conversion_error;
  std::optional<PixelBuffer> converted;
  ConstPixelSpan pixels = frame->pixels.span();
  if (pixels.format.model != ColorModel::rgba ||
      pixels.format.bits_per_sample != 8) {
    converted = converter.convert(pixels, rgba, conversion_error);
    if (!converted) {
      result.error = std::move(conversion_error);
      return result;
    }
    pixels = static_cast<const PixelBuffer &>(*converted).span();
  }
  bool alpha = options.write_alpha;
  size_t bytes_per_pixel = alpha ? 4 : 3;
  size_t row_bytes = size_t(image.width) * bytes_per_pixel;
  size_t disk_stride = (row_bytes + 3) & ~size_t(3);
  size_t pixel_bytes = disk_stride * image.height;
  size_t header_size = 14 + (alpha ? 56 : 40);
  if (pixel_bytes > std::numeric_limits<uint32_t>::max() - header_size) {
    result.error = {ErrorCode::integer_overflow, 0,
                    "encoded BMP exceeds 32-bit file size"};
    return result;
  }
  result.bytes.reserve(header_size + pixel_bytes);
  result.bytes.push_back('B');
  result.bytes.push_back('M');
  internal::write_u32(result.bytes, uint32_t(header_size + pixel_bytes),
                      ByteOrder::little);
  internal::write_u32(result.bytes, 0, ByteOrder::little);
  internal::write_u32(result.bytes, uint32_t(header_size), ByteOrder::little);
  internal::write_u32(result.bytes, alpha ? 56 : 40, ByteOrder::little);
  internal::write_u32(result.bytes, image.width, ByteOrder::little);
  internal::write_u32(result.bytes, image.height, ByteOrder::little);
  internal::write_u16(result.bytes, 1, ByteOrder::little);
  internal::write_u16(result.bytes, alpha ? 32 : 24, ByteOrder::little);
  internal::write_u32(result.bytes, alpha ? bmp_bitfields : bmp_rgb,
                      ByteOrder::little);
  internal::write_u32(result.bytes, uint32_t(pixel_bytes), ByteOrder::little);
  internal::write_u32(result.bytes, 2835, ByteOrder::little);
  internal::write_u32(result.bytes, 2835, ByteOrder::little);
  internal::write_u32(result.bytes, 0, ByteOrder::little);
  internal::write_u32(result.bytes, 0, ByteOrder::little);
  if (alpha) {
    internal::write_u32(result.bytes, 0x00ff0000, ByteOrder::little);
    internal::write_u32(result.bytes, 0x0000ff00, ByteOrder::little);
    internal::write_u32(result.bytes, 0x000000ff, ByteOrder::little);
    internal::write_u32(result.bytes, 0xff000000, ByteOrder::little);
  }
  std::vector<uint8_t> padding(disk_stride - row_bytes, 0);
  for (uint32_t y = image.height; y-- > 0;) {
    const uint8_t *source = pixels.data + size_t(y) * pixels.stride;
    for (uint32_t x = 0; x < image.width; ++x) {
      result.bytes.push_back(source[x * 4 + 2]);
      result.bytes.push_back(source[x * 4 + 1]);
      result.bytes.push_back(source[x * 4]);
      if (alpha)
        result.bytes.push_back(source[x * 4 + 3]);
    }
    result.bytes.insert(result.bytes.end(), padding.begin(), padding.end());
  }
  return result;
}

} // namespace pixelforge
