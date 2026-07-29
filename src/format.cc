#include "pixelforge/image.h"

#include <algorithm>
#include <cstring>

namespace pixelforge {

FormatGuess FormatDetector::detect(const uint8_t *data, size_t size) {
  if (!data && size)
    return {};
  static constexpr uint8_t png_signature[8] = {0x89, 'P', 'N', 'G',
                                                0x0d, 0x0a, 0x1a, 0x0a};
  if (size >= 8 && std::memcmp(data, png_signature, 8) == 0)
    return {Format::png, 100, "PNG eight-byte signature"};
  if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
    if (size >= 14)
      return {Format::bmp, 98, "BMP file header"};
    return {Format::bmp, 70, "truncated BMP signature"};
  }
  if (size >= 4) {
    bool classic_little =
        data[0] == 'I' && data[1] == 'I' && data[2] == 42 && data[3] == 0;
    bool classic_big =
        data[0] == 'M' && data[1] == 'M' && data[2] == 0 && data[3] == 42;
    bool big_little =
        data[0] == 'I' && data[1] == 'I' && data[2] == 43 && data[3] == 0;
    bool big_big =
        data[0] == 'M' && data[1] == 'M' && data[2] == 0 && data[3] == 43;
    if (classic_little || classic_big)
      return {Format::tiff, 100, "classic TIFF byte order and version"};
    if (big_little || big_big)
      return {Format::tiff, 100, "BigTIFF byte order and version"};
  }
  if (size >= 8 && std::memcmp(data, "PFTILE\r\n", 8) == 0)
    return {Format::pft, 100, "PixelForge tiled image signature"};
  if (size >= 4 && std::memcmp(data, "PFTI", 4) == 0)
    return {Format::pft, 65, "partial PixelForge tiled signature"};
  return {Format::unknown, 0, "no recognized image signature"};
}

FormatGuess FormatDetector::detect(std::string_view bytes) {
  return detect(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
}

std::string_view FormatDetector::name(Format format) {
  switch (format) {
  case Format::png:
    return "PNG";
  case Format::bmp:
    return "BMP";
  case Format::tiff:
    return "TIFF";
  case Format::pft:
    return "PixelForge Tiled Image";
  case Format::unknown:
    return "Unknown";
  }
  return "Unknown";
}

std::string_view FormatDetector::extension(Format format) {
  switch (format) {
  case Format::png:
    return "png";
  case Format::bmp:
    return "bmp";
  case Format::tiff:
    return "tiff";
  case Format::pft:
    return "pft";
  case Format::unknown:
    return "";
  }
  return "";
}

std::string_view FormatDetector::mime_type(Format format) {
  switch (format) {
  case Format::png:
    return "image/png";
  case Format::bmp:
    return "image/bmp";
  case Format::tiff:
    return "image/tiff";
  case Format::pft:
    return "image/x-pixelforge-tiled";
  case Format::unknown:
    return "application/octet-stream";
  }
  return "application/octet-stream";
}

double Rational::value() const {
  return denominator ? double(numerator) / double(denominator) : 0.0;
}

} // namespace pixelforge
