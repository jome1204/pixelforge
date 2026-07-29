#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pixelforge {

enum class ErrorCode {
  none,
  truncated,
  invalid_signature,
  invalid_header,
  invalid_chunk,
  invalid_directory,
  invalid_tag,
  invalid_palette,
  invalid_pixels,
  invalid_metadata,
  checksum_mismatch,
  unsupported_format,
  unsupported_feature,
  integer_overflow,
  resource_limit,
  allocation_failed,
  internal_error
};

struct Error {
  ErrorCode code = ErrorCode::none;
  size_t offset = 0;
  std::string message;
  explicit operator bool() const { return code != ErrorCode::none; }
};

struct Limits {
  size_t max_input_bytes = 128u * 1024u * 1024u;
  uint32_t max_width = 32768;
  uint32_t max_height = 32768;
  uint64_t max_pixels = 268435456;
  size_t max_decoded_bytes = 512u * 1024u * 1024u;
  size_t max_metadata_bytes = 16u * 1024u * 1024u;
  size_t max_chunk_count = 100000;
  size_t max_directory_count = 65536;
  size_t max_tag_count = 1000000;
  size_t max_frames = 4096;
  size_t max_palette_entries = 65536;
  size_t max_tiles = 1048576;
  size_t max_recursion = 64;
  size_t max_text_length = 1048576;
};

enum class Format { unknown, png, bmp, tiff, pft };
enum class ByteOrder { little, big };
enum class ColorModel {
  unknown,
  grayscale,
  grayscale_alpha,
  indexed,
  rgb,
  rgba,
  cmyk,
  ycbcr,
  lab
};
enum class SampleType { unsigned_integer, signed_integer, floating_point };
enum class AlphaMode { none, straight, premultiplied };
enum class Orientation {
  top_left = 1,
  top_right = 2,
  bottom_right = 3,
  bottom_left = 4,
  left_top = 5,
  right_top = 6,
  right_bottom = 7,
  left_bottom = 8
};
enum class Compression {
  none,
  deflate,
  packbits,
  lzw,
  rle4,
  rle8,
  pft_delta,
  unsupported
};

struct FormatGuess {
  Format format = Format::unknown;
  unsigned confidence = 0;
  std::string reason;
};

class FormatDetector {
public:
  static FormatGuess detect(const uint8_t *, size_t);
  static FormatGuess detect(std::string_view);
  static std::string_view name(Format);
  static std::string_view extension(Format);
  static std::string_view mime_type(Format);
};

struct Rational {
  int64_t numerator = 0;
  int64_t denominator = 1;
  double value() const;
  bool valid() const { return denominator != 0; }
};

using MetadataValue =
    std::variant<std::string, int64_t, uint64_t, double, Rational,
                 std::vector<uint8_t>, std::vector<int64_t>,
                 std::vector<uint64_t>, std::vector<double>>;

struct MetadataEntry {
  std::string key;
  MetadataValue value;
  std::string namespace_name;
  std::string language;
  bool binary = false;
  size_t source_offset = 0;
};

class Metadata {
public:
  bool add(MetadataEntry, const Limits &, Error &);
  bool set(std::string key, MetadataValue value, const Limits &, Error &);
  const MetadataEntry *find(std::string_view) const;
  std::vector<const MetadataEntry *> find_all(std::string_view) const;
  bool erase(std::string_view);
  void clear();
  size_t size() const { return entries_.size(); }
  size_t byte_size() const;
  const std::vector<MetadataEntry> &entries() const { return entries_; }
  std::string text(std::string_view key, std::string fallback = {}) const;
  std::optional<int64_t> integer(std::string_view key) const;

private:
  std::vector<MetadataEntry> entries_;
};

struct ColorProfile {
  std::string name;
  std::vector<uint8_t> icc;
  double gamma = 0.0;
  std::array<double, 8> chromaticities{};
  bool has_chromaticities = false;
};

struct PixelFormat {
  ColorModel model = ColorModel::rgba;
  SampleType sample_type = SampleType::unsigned_integer;
  uint8_t bits_per_sample = 8;
  uint8_t channels = 4;
  AlphaMode alpha = AlphaMode::straight;
  ByteOrder byte_order = ByteOrder::little;

  size_t bytes_per_pixel() const;
  bool byte_aligned() const;
  bool valid() const;
  bool has_alpha() const;
};

struct PixelSpan {
  uint8_t *data = nullptr;
  size_t size = 0;
  size_t stride = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format;
};

struct ConstPixelSpan {
  const uint8_t *data = nullptr;
  size_t size = 0;
  size_t stride = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format;
};

class PixelBuffer {
public:
  PixelBuffer() = default;
  PixelBuffer(PixelBuffer &&) noexcept = default;
  PixelBuffer &operator=(PixelBuffer &&) noexcept = default;
  PixelBuffer(const PixelBuffer &) = delete;
  PixelBuffer &operator=(const PixelBuffer &) = delete;

  static std::optional<PixelBuffer> allocate(uint32_t width, uint32_t height,
                                             PixelFormat, const Limits &,
                                             Error &);
  static std::optional<PixelBuffer>
  copy_from(ConstPixelSpan, const Limits &, Error &);
  uint8_t *row(uint32_t y);
  const uint8_t *row(uint32_t y) const;
  PixelSpan span();
  ConstPixelSpan span() const;
  void clear(uint8_t value = 0);
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  size_t stride() const { return stride_; }
  size_t size() const { return bytes_.size(); }
  const PixelFormat &format() const { return format_; }
  std::vector<uint8_t> &bytes() { return bytes_; }
  const std::vector<uint8_t> &bytes() const { return bytes_; }

private:
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  size_t stride_ = 0;
  PixelFormat format_;
  std::vector<uint8_t> bytes_;
};

struct PaletteEntry {
  uint16_t red = 0;
  uint16_t green = 0;
  uint16_t blue = 0;
  uint16_t alpha = 65535;
};

class Palette {
public:
  bool append(PaletteEntry, const Limits &, Error &);
  bool set(size_t, PaletteEntry, Error &);
  const PaletteEntry *at(size_t) const;
  size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }
  const std::vector<PaletteEntry> &entries() const { return entries_; }
  std::optional<PixelBuffer> expand(ConstPixelSpan indices, const Limits &,
                                    Error &) const;

private:
  std::vector<PaletteEntry> entries_;
};

enum class BlendMode { source, over };
enum class DisposalMode { none, background, previous };

struct FrameTiming {
  uint32_t numerator = 0;
  uint32_t denominator = 100;
  double seconds() const;
};

struct ImageFrame {
  PixelBuffer pixels;
  uint32_t x = 0;
  uint32_t y = 0;
  FrameTiming delay;
  BlendMode blend = BlendMode::source;
  DisposalMode disposal = DisposalMode::none;
  Metadata metadata;
};

struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format;
  Orientation orientation = Orientation::top_left;
  std::vector<ImageFrame> frames;
  Palette palette;
  Metadata metadata;
  ColorProfile profile;
  uint32_t loop_count = 0;

  bool valid(const Limits &, Error &) const;
  bool animated() const { return frames.size() > 1; }
  ImageFrame *primary_frame();
  const ImageFrame *primary_frame() const;
};

struct DecodeOptions {
  bool decode_pixels = true;
  bool decode_metadata = true;
  bool apply_orientation = false;
  bool expand_palette = true;
  bool compose_frames = false;
  bool validate_checksums = true;
  std::optional<Format> format_hint;
};

struct EncodeOptions {
  Compression compression = Compression::none;
  bool preserve_metadata = true;
  bool write_alpha = true;
  bool deterministic = true;
  int compression_level = 6;
};

struct DecodeResult {
  std::optional<Image> image;
  Error error;
  std::vector<Error> warnings;
  explicit operator bool() const { return image.has_value() && !error; }
};

struct EncodeResult {
  std::vector<uint8_t> bytes;
  Error error;
  explicit operator bool() const { return !error; }
};

class Decoder {
public:
  explicit Decoder(Limits limits = {});
  DecodeResult decode(const uint8_t *, size_t, DecodeOptions = {}) const;
  DecodeResult decode(std::string_view, DecodeOptions = {}) const;
  const Limits &limits() const { return limits_; }

private:
  Limits limits_;
};

class Encoder {
public:
  explicit Encoder(Limits limits = {});
  EncodeResult encode(const Image &, Format, EncodeOptions = {}) const;

private:
  Limits limits_;
};

struct PngChunk {
  uint32_t length = 0;
  std::array<char, 4> type{};
  size_t offset = 0;
  size_t data_offset = 0;
  uint32_t crc = 0;
  std::vector<uint8_t> data;
  std::string type_name() const;
  bool critical() const;
  bool private_chunk() const;
  bool safe_to_copy() const;
};

struct PngHeader {
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t bit_depth = 0;
  uint8_t color_type = 0;
  uint8_t compression_method = 0;
  uint8_t filter_method = 0;
  uint8_t interlace_method = 0;
  uint8_t channels() const;
  bool valid(Error &) const;
};

struct PngAnimationControl {
  uint32_t frame_count = 0;
  uint32_t loop_count = 0;
};

struct PngFrameControl {
  uint32_t sequence = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t x = 0;
  uint32_t y = 0;
  uint16_t delay_numerator = 0;
  uint16_t delay_denominator = 0;
  uint8_t disposal = 0;
  uint8_t blend = 0;
};

struct PngDocument {
  PngHeader header;
  std::vector<PngChunk> chunks;
  std::optional<PngAnimationControl> animation;
  std::vector<PngFrameControl> frame_controls;
  Palette palette;
  Metadata metadata;
  ColorProfile profile;
  std::vector<uint8_t> compressed_pixels;
  std::vector<Error> warnings;
};

class PngChunkReader {
public:
  explicit PngChunkReader(Limits limits = {});
  std::optional<PngDocument> parse(const uint8_t *, size_t, Error &,
                                   bool check_crc = true) const;
  std::optional<std::vector<PngChunk>>
  parse_chunk_sequence(const uint8_t *, size_t, Error &,
                       bool check_crc = true) const;
  static uint32_t crc32(const uint8_t *, size_t, uint32_t seed = 0);
  static bool valid_type(const std::array<char, 4> &);

private:
  Limits limits_;
};

class PngDecoder {
public:
  explicit PngDecoder(Limits limits = {});
  DecodeResult decode(const uint8_t *, size_t, DecodeOptions = {}) const;

private:
  std::optional<PixelBuffer> decode_frame(const PngDocument &,
                                          const std::vector<uint8_t> &,
                                          Error &) const;
  bool unfilter(std::vector<uint8_t> &, uint32_t, uint32_t, uint8_t,
                uint8_t, Error &) const;
  Limits limits_;
};

class PngEncoder {
public:
  explicit PngEncoder(Limits limits = {});
  EncodeResult encode(const Image &, EncodeOptions = {}) const;

private:
  Limits limits_;
};

struct BmpHeader {
  uint32_t file_size = 0;
  uint32_t pixel_offset = 0;
  uint32_t dib_size = 0;
  int32_t width = 0;
  int32_t height = 0;
  uint16_t planes = 0;
  uint16_t bits_per_pixel = 0;
  uint32_t compression = 0;
  uint32_t image_size = 0;
  int32_t x_pixels_per_meter = 0;
  int32_t y_pixels_per_meter = 0;
  uint32_t colors_used = 0;
  uint32_t important_colors = 0;
  uint32_t red_mask = 0;
  uint32_t green_mask = 0;
  uint32_t blue_mask = 0;
  uint32_t alpha_mask = 0;
  bool top_down() const { return height < 0; }
  uint32_t absolute_height() const;
};

class BmpDecoder {
public:
  explicit BmpDecoder(Limits limits = {});
  std::optional<BmpHeader> parse_header(const uint8_t *, size_t, Error &) const;
  DecodeResult decode(const uint8_t *, size_t, DecodeOptions = {}) const;

private:
  bool decode_uncompressed(const uint8_t *, size_t, const BmpHeader &,
                           const Palette &, PixelBuffer &, Error &) const;
  bool decode_rle(const uint8_t *, size_t, const BmpHeader &, PixelBuffer &,
                  Error &) const;
  Limits limits_;
};

class BmpEncoder {
public:
  explicit BmpEncoder(Limits limits = {});
  EncodeResult encode(const Image &, EncodeOptions = {}) const;

private:
  Limits limits_;
};

enum class TiffType : uint16_t {
  byte = 1,
  ascii = 2,
  short_value = 3,
  long_value = 4,
  rational = 5,
  signed_byte = 6,
  undefined = 7,
  signed_short = 8,
  signed_long = 9,
  signed_rational = 10,
  floating = 11,
  double_value = 12,
  ifd = 13,
  long8 = 16,
  signed_long8 = 17,
  ifd8 = 18
};

struct TiffEntry {
  uint16_t tag = 0;
  TiffType type = TiffType::byte;
  uint64_t count = 0;
  uint64_t value_or_offset = 0;
  size_t source_offset = 0;
  std::vector<uint8_t> value_bytes;
  size_t element_size() const;
  std::optional<uint64_t> unsigned_value(size_t, ByteOrder) const;
  std::optional<int64_t> signed_value(size_t, ByteOrder) const;
  std::optional<Rational> rational_value(size_t, ByteOrder) const;
  std::string ascii_value() const;
};

struct TiffDirectory {
  uint64_t offset = 0;
  uint64_t next_offset = 0;
  std::vector<TiffEntry> entries;
  std::vector<TiffDirectory> children;
  const TiffEntry *find(uint16_t tag) const;
  std::vector<const TiffEntry *> find_all(uint16_t tag) const;
};

struct TiffDocument {
  ByteOrder byte_order = ByteOrder::little;
  bool big_tiff = false;
  uint64_t first_directory_offset = 0;
  std::vector<TiffDirectory> directories;
  Metadata metadata;
  std::vector<Error> warnings;
};

class TiffDirectoryReader {
public:
  explicit TiffDirectoryReader(Limits limits = {});
  std::optional<TiffDocument> parse(const uint8_t *, size_t, Error &) const;

private:
  bool parse_directory(const uint8_t *, size_t, ByteOrder, bool, uint64_t,
                       TiffDirectory &, std::set<uint64_t> &, size_t &,
                       Error &, size_t) const;
  bool load_value(const uint8_t *, size_t, ByteOrder, bool, TiffEntry &,
                  Error &) const;
  Limits limits_;
};

class TiffDecoder {
public:
  explicit TiffDecoder(Limits limits = {});
  DecodeResult decode(const uint8_t *, size_t, DecodeOptions = {}) const;

private:
  bool decode_directory(const uint8_t *, size_t, const TiffDocument &,
                        const TiffDirectory &, ImageFrame &, Image &, Error &)
      const;
  Limits limits_;
};

struct TileIndexEntry {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t frame = 0;
  uint64_t offset = 0;
  uint32_t encoded_size = 0;
  uint32_t decoded_size = 0;
  uint32_t checksum = 0;
  uint16_t flags = 0;
};

struct PftHeader {
  uint16_t version = 0;
  uint16_t header_size = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint16_t tile_width = 0;
  uint16_t tile_height = 0;
  uint16_t frame_count = 0;
  uint8_t channels = 0;
  uint8_t bits_per_sample = 0;
  ColorModel color_model = ColorModel::unknown;
  Compression compression = Compression::none;
  uint32_t tile_count = 0;
  uint64_t index_offset = 0;
  uint64_t metadata_offset = 0;
  uint32_t metadata_size = 0;
};

class PftDecoder {
public:
  explicit PftDecoder(Limits limits = {});
  std::optional<PftHeader> parse_header(const uint8_t *, size_t, Error &) const;
  std::optional<std::vector<TileIndexEntry>>
  parse_index(const uint8_t *, size_t, const PftHeader &, Error &) const;
  DecodeResult decode(const uint8_t *, size_t, DecodeOptions = {}) const;

private:
  bool decode_tile(const uint8_t *, size_t, const PftHeader &,
                   const TileIndexEntry &, PixelBuffer &, Error &) const;
  Limits limits_;
};

class PftEncoder {
public:
  explicit PftEncoder(Limits limits = {});
  EncodeResult encode(const Image &, EncodeOptions = {}) const;

private:
  Limits limits_;
};

enum class ResampleFilter { nearest, bilinear, bicubic, lanczos3 };
enum class Rotation { clockwise_90, clockwise_180, clockwise_270 };

struct Rectangle {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct TransformOptions {
  ResampleFilter resample = ResampleFilter::bilinear;
  bool linear_light = false;
  bool preserve_aspect = false;
  std::array<uint16_t, 4> background = {0, 0, 0, 0};
};

class ImageTransform {
public:
  explicit ImageTransform(Limits limits = {});
  std::optional<PixelBuffer> crop(ConstPixelSpan, Rectangle, Error &) const;
  std::optional<PixelBuffer> rotate(ConstPixelSpan, Rotation, Error &) const;
  std::optional<PixelBuffer> flip_horizontal(ConstPixelSpan, Error &) const;
  std::optional<PixelBuffer> flip_vertical(ConstPixelSpan, Error &) const;
  std::optional<PixelBuffer> resize(ConstPixelSpan, uint32_t, uint32_t,
                                    TransformOptions, Error &) const;
  std::optional<PixelBuffer> orient(ConstPixelSpan, Orientation, Error &) const;
  std::optional<PixelBuffer> composite(ConstPixelSpan, ConstPixelSpan, int64_t,
                                       int64_t, BlendMode, Error &) const;

private:
  Limits limits_;
};

struct Color {
  double red = 0;
  double green = 0;
  double blue = 0;
  double alpha = 1;
};

class ColorConverter {
public:
  explicit ColorConverter(Limits limits = {});
  std::optional<PixelBuffer> convert(ConstPixelSpan, PixelFormat, Error &) const;
  bool premultiply(PixelSpan, Error &) const;
  bool unpremultiply(PixelSpan, Error &) const;
  static Color rgb_to_hsl(Color);
  static Color hsl_to_rgb(Color);
  static std::array<double, 3> rgb_to_xyz(Color);
  static Color xyz_to_rgb(const std::array<double, 3> &);
  static std::array<double, 3> rgb_to_lab(Color);
  static Color lab_to_rgb(const std::array<double, 3> &, double alpha = 1);

private:
  Color read_pixel(ConstPixelSpan, uint32_t, uint32_t, Error &) const;
  bool write_pixel(PixelSpan, uint32_t, uint32_t, Color, Error &) const;
  Limits limits_;
};

struct Histogram {
  std::array<std::vector<uint64_t>, 4> channels;
  uint64_t pixel_count = 0;
  std::array<double, 4> mean{};
  std::array<double, 4> variance{};
  std::array<uint16_t, 4> minimum{};
  std::array<uint16_t, 4> maximum{};
};

struct ImageStatistics {
  Histogram histogram;
  double entropy = 0;
  double opaque_fraction = 0;
  double transparent_fraction = 0;
  uint64_t unique_color_estimate = 0;
};

class ImageAnalyzer {
public:
  explicit ImageAnalyzer(Limits limits = {});
  std::optional<ImageStatistics> analyze(ConstPixelSpan, Error &) const;
  std::optional<Rectangle> alpha_bounds(ConstPixelSpan, Error &) const;
  uint64_t perceptual_hash(ConstPixelSpan, Error &) const;

private:
  Limits limits_;
};

struct ValidationIssue {
  enum class Severity { information, warning, error } severity =
      Severity::information;
  ErrorCode code = ErrorCode::none;
  std::string subsystem;
  size_t offset = 0;
  std::string message;
};

struct ValidationReport {
  Format format = Format::unknown;
  std::vector<ValidationIssue> issues;
  size_t structures_checked = 0;
  size_t pixels_checked = 0;
  size_t metadata_entries_checked = 0;
  bool valid() const;
};

class ImageValidator {
public:
  explicit ImageValidator(Limits limits = {});
  ValidationReport validate_encoded(const uint8_t *, size_t,
                                    std::optional<Format> = {}) const;
  ValidationReport validate_decoded(const Image &) const;

private:
  Limits limits_;
};

class ReportFormatter {
public:
  static std::string text(const ValidationReport &);
  static std::string json(const ValidationReport &);
  static std::string image_json(const Image &);
  static std::string error_name(ErrorCode);
  static std::string color_model_name(ColorModel);

private:
  static std::string quote(std::string_view);
};

} // namespace pixelforge
