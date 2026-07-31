#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace GX {

enum class TextureFormat : uint32_t {
    I4 = 0,
    I8 = 1,
    IA4 = 2,
    IA8 = 3,
    RGB565 = 4,
    RGB5A3 = 5,
    RGBA8 = 6,
    C4 = 8,
    C8 = 9,
    CMPR = 14,
};

enum class TlutFormat : uint32_t {
    IA8 = 0,
    RGB565 = 1,
    RGB5A3 = 2,
};

enum class TlutByteOrder {
    BigEndian,
    NativeEndian,
};

struct TlutView {
    std::span<const uint8_t> data;
    TlutFormat format = TlutFormat::IA8;
    std::size_t entryCount = 0;
    TlutByteOrder byteOrder = TlutByteOrder::BigEndian;
};

enum class TextureDecodeError {
    None,
    InvalidDimensions,
    UnsupportedFormat,
    UnsupportedTlutFormat,
    SourceTooSmall,
    DestinationTooSmall,
    MissingTlut,
    TlutTooSmall,
    TlutIndexOutOfRange,
};

std::size_t GetEncodedTextureSize(uint32_t width, uint32_t height, TextureFormat format);
std::size_t GetDecodedTextureSize(uint32_t width, uint32_t height);

TextureDecodeError DecodeTexture(std::span<const uint8_t> source, uint32_t width, uint32_t height, TextureFormat format,
                                 std::span<uint8_t> destination, const TlutView* tlut = nullptr);

} // namespace GX
