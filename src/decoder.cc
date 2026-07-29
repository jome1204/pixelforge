#include "pixelforge/image.h"

namespace pixelforge {

Decoder::Decoder(Limits limits) : limits_(limits) {}

DecodeResult Decoder::decode(const uint8_t *data, size_t size,
                             DecodeOptions options) const {
  DecodeResult result;
  if (!data && size) {
    result.error = {ErrorCode::invalid_header, 0, "null input pointer"};
    return result;
  }
  if (size > limits_.max_input_bytes) {
    result.error = {ErrorCode::resource_limit, 0,
                    "input exceeds configured byte limit"};
    return result;
  }
  Format format =
      options.format_hint.value_or(FormatDetector::detect(data, size).format);
  switch (format) {
  case Format::png:
    return PngDecoder(limits_).decode(data, size, options);
  case Format::bmp:
    return BmpDecoder(limits_).decode(data, size, options);
  case Format::tiff:
    return TiffDecoder(limits_).decode(data, size, options);
  case Format::pft:
    return PftDecoder(limits_).decode(data, size, options);
  case Format::unknown:
    result.error = {ErrorCode::unsupported_format, 0,
                    "input format could not be detected"};
    return result;
  }
  result.error = {ErrorCode::unsupported_format, 0, "unsupported image format"};
  return result;
}

DecodeResult Decoder::decode(std::string_view bytes,
                             DecodeOptions options) const {
  return decode(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(),
                options);
}

Encoder::Encoder(Limits limits) : limits_(limits) {}

EncodeResult Encoder::encode(const Image &image, Format format,
                             EncodeOptions options) const {
  switch (format) {
  case Format::png:
    return PngEncoder(limits_).encode(image, options);
  case Format::bmp:
    return BmpEncoder(limits_).encode(image, options);
  case Format::pft:
    return PftEncoder(limits_).encode(image, options);
  case Format::tiff:
    return {{},
            {ErrorCode::unsupported_feature, 0,
             "TIFF encoding is not currently available"}};
  case Format::unknown:
    return {{},
            {ErrorCode::unsupported_format, 0,
             "an output image format is required"}};
  }
  return {{},
          {ErrorCode::unsupported_format, 0, "unsupported output format"}};
}

} // namespace pixelforge
