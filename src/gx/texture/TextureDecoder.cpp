// The texture decoding routines in this file are derived from ACGC-PC-Port by
// FlyingMeta and are used under the following license:
//
// MIT License
//
// Copyright (c) 2026 FlyingMeta
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "gx/texture/TextureDecoder.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace GX {
namespace {

using Rgba = std::array<uint8_t, 4>;
using Palette = std::array<Rgba, 256>;

struct BlockLayout {
    uint32_t width;
    uint32_t height;
    std::size_t bytes;
};

bool TryMultiply(std::size_t lhs, std::size_t rhs, std::size_t& result) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

std::size_t DivideRoundingUp(uint32_t value, uint32_t divisor) {
    return (static_cast<std::size_t>(value) + divisor - 1) / divisor;
}

bool GetBlockLayout(TextureFormat format, BlockLayout& layout) {
    switch (format) {
        case TextureFormat::I4:
        case TextureFormat::C4:
        case TextureFormat::CMPR:
            layout = { 8, 8, 32 };
            return true;
        case TextureFormat::I8:
        case TextureFormat::IA4:
        case TextureFormat::C8:
            layout = { 8, 4, 32 };
            return true;
        case TextureFormat::IA8:
        case TextureFormat::RGB565:
        case TextureFormat::RGB5A3:
            layout = { 4, 4, 32 };
            return true;
        case TextureFormat::RGBA8:
            layout = { 4, 4, 64 };
            return true;
        default:
            return false;
    }
}

bool IsSupportedTlutFormat(TlutFormat format) {
    switch (format) {
        case TlutFormat::IA8:
        case TlutFormat::RGB565:
        case TlutFormat::RGB5A3:
            return true;
        default:
            return false;
    }
}

uint16_t ReadBigEndian16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint16_t ReadNative16(const uint8_t* data) {
    uint16_t value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

Rgba DecodeRgb565(uint16_t value) {
    const uint8_t red = static_cast<uint8_t>((value >> 11) & 0x1F);
    const uint8_t green = static_cast<uint8_t>((value >> 5) & 0x3F);
    const uint8_t blue = static_cast<uint8_t>(value & 0x1F);
    return {
        static_cast<uint8_t>((red << 3) | (red >> 2)),
        static_cast<uint8_t>((green << 2) | (green >> 4)),
        static_cast<uint8_t>((blue << 3) | (blue >> 2)),
        255,
    };
}

Rgba DecodeRgb5A3(uint16_t value) {
    if ((value & 0x8000) != 0) {
        const uint8_t red = static_cast<uint8_t>((value >> 10) & 0x1F);
        const uint8_t green = static_cast<uint8_t>((value >> 5) & 0x1F);
        const uint8_t blue = static_cast<uint8_t>(value & 0x1F);
        return {
            static_cast<uint8_t>((red << 3) | (red >> 2)),
            static_cast<uint8_t>((green << 3) | (green >> 2)),
            static_cast<uint8_t>((blue << 3) | (blue >> 2)),
            255,
        };
    }

    const uint8_t alpha = static_cast<uint8_t>((value >> 12) & 0x07);
    const uint8_t red = static_cast<uint8_t>((value >> 8) & 0x0F);
    const uint8_t green = static_cast<uint8_t>((value >> 4) & 0x0F);
    const uint8_t blue = static_cast<uint8_t>(value & 0x0F);
    return {
        static_cast<uint8_t>((red << 4) | red),
        static_cast<uint8_t>((green << 4) | green),
        static_cast<uint8_t>((blue << 4) | blue),
        static_cast<uint8_t>((alpha << 5) | (alpha << 2) | (alpha >> 1)),
    };
}

void StorePixel(std::span<uint8_t> destination, std::size_t width, std::size_t x, std::size_t y, const Rgba& color) {
    const std::size_t offset = (y * width + x) * 4;
    std::copy(color.begin(), color.end(), destination.begin() + offset);
}

Palette BuildPalette(const TlutView* tlut) {
    Palette palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        palette[i] = { static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 255 };
    }

    if (tlut == nullptr || tlut->entryCount == 0) {
        return palette;
    }

    const std::size_t entryCount = std::min(tlut->entryCount, palette.size());
    for (std::size_t i = 0; i < entryCount; ++i) {
        const uint8_t* entry = tlut->data.data() + i * 2;
        const uint16_t value =
            tlut->byteOrder == TlutByteOrder::BigEndian ? ReadBigEndian16(entry) : ReadNative16(entry);
        switch (tlut->format) {
            case TlutFormat::RGB5A3:
                palette[i] = DecodeRgb5A3(value);
                break;
            case TlutFormat::RGB565:
                palette[i] = DecodeRgb565(value);
                break;
            case TlutFormat::IA8:
                palette[i] = { static_cast<uint8_t>(value), static_cast<uint8_t>(value), static_cast<uint8_t>(value),
                               static_cast<uint8_t>(value >> 8) };
                break;
        }
    }
    return palette;
}

void DecodeI4(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 8);
    const std::size_t blockHeight = DivideRoundingUp(height, 8);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 8; ++y) {
                for (std::size_t x = 0; x < 8; x += 2) {
                    const uint8_t value = *source++;
                    const std::size_t pixelX = blockX * 8 + x;
                    const std::size_t pixelY = blockY * 8 + y;
                    const uint8_t first = static_cast<uint8_t>((value >> 4) | (value & 0xF0));
                    const uint8_t second = static_cast<uint8_t>((value & 0x0F) | ((value & 0x0F) << 4));
                    if (pixelX < width && pixelY < height) {
                        StorePixel(destination, width, pixelX, pixelY, { first, first, first, first });
                    }
                    if (pixelX + 1 < width && pixelY < height) {
                        StorePixel(destination, width, pixelX + 1, pixelY, { second, second, second, second });
                    }
                }
            }
        }
    }
}

void DecodeI8(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 8);
    const std::size_t blockHeight = DivideRoundingUp(height, 4);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 4; ++y) {
                for (std::size_t x = 0; x < 8; ++x) {
                    const uint8_t value = *source++;
                    const std::size_t pixelX = blockX * 8 + x;
                    const std::size_t pixelY = blockY * 4 + y;
                    if (pixelX < width && pixelY < height) {
                        StorePixel(destination, width, pixelX, pixelY, { value, value, value, value });
                    }
                }
            }
        }
    }
}

void DecodeIa4(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 8);
    const std::size_t blockHeight = DivideRoundingUp(height, 4);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 4; ++y) {
                for (std::size_t x = 0; x < 8; ++x) {
                    const uint8_t value = *source++;
                    const std::size_t pixelX = blockX * 8 + x;
                    const std::size_t pixelY = blockY * 4 + y;
                    if (pixelX < width && pixelY < height) {
                        const uint8_t alpha = static_cast<uint8_t>((value >> 4) | (value & 0xF0));
                        const uint8_t intensity = static_cast<uint8_t>((value & 0x0F) | ((value & 0x0F) << 4));
                        StorePixel(destination, width, pixelX, pixelY, { intensity, intensity, intensity, alpha });
                    }
                }
            }
        }
    }
}

void DecodeIa8(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 4);
    const std::size_t blockHeight = DivideRoundingUp(height, 4);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 4; ++y) {
                for (std::size_t x = 0; x < 4; ++x) {
                    const uint8_t alpha = *source++;
                    const uint8_t intensity = *source++;
                    const std::size_t pixelX = blockX * 4 + x;
                    const std::size_t pixelY = blockY * 4 + y;
                    if (pixelX < width && pixelY < height) {
                        StorePixel(destination, width, pixelX, pixelY, { intensity, intensity, intensity, alpha });
                    }
                }
            }
        }
    }
}

void DecodeRgb565(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 4);
    const std::size_t blockHeight = DivideRoundingUp(height, 4);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 4; ++y) {
                for (std::size_t x = 0; x < 4; ++x) {
                    const Rgba color = DecodeRgb565(ReadBigEndian16(source));
                    source += 2;
                    const std::size_t pixelX = blockX * 4 + x;
                    const std::size_t pixelY = blockY * 4 + y;
                    if (pixelX < width && pixelY < height) {
                        StorePixel(destination, width, pixelX, pixelY, color);
                    }
                }
            }
        }
    }
}

void DecodeRgb5A3(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 4);
    const std::size_t blockHeight = DivideRoundingUp(height, 4);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 4; ++y) {
                for (std::size_t x = 0; x < 4; ++x) {
                    const Rgba color = DecodeRgb5A3(ReadBigEndian16(source));
                    source += 2;
                    const std::size_t pixelX = blockX * 4 + x;
                    const std::size_t pixelY = blockY * 4 + y;
                    if (pixelX < width && pixelY < height) {
                        StorePixel(destination, width, pixelX, pixelY, color);
                    }
                }
            }
        }
    }
}

void DecodeRgba8(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 4);
    const std::size_t blockHeight = DivideRoundingUp(height, 4);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            std::array<std::array<uint8_t, 2>, 16> alphaRed{};
            for (auto& pixel : alphaRed) {
                pixel[0] = *source++;
                pixel[1] = *source++;
            }
            for (std::size_t i = 0; i < 16; ++i) {
                const std::size_t x = i % 4;
                const std::size_t y = i / 4;
                const uint8_t green = *source++;
                const uint8_t blue = *source++;
                const std::size_t pixelX = blockX * 4 + x;
                const std::size_t pixelY = blockY * 4 + y;
                if (pixelX < width && pixelY < height) {
                    StorePixel(destination, width, pixelX, pixelY, { alphaRed[i][1], green, blue, alphaRed[i][0] });
                }
            }
        }
    }
}

uint8_t BlendCmpr(uint8_t primary, uint8_t secondary) {
    return static_cast<uint8_t>((primary * 5 + secondary * 3) >> 3);
}

void DecodeCmpr(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination) {
    const std::size_t blockWidth = DivideRoundingUp(width, 8);
    const std::size_t blockHeight = DivideRoundingUp(height, 8);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t subBlock = 0; subBlock < 4; ++subBlock) {
                const std::size_t subX = (subBlock & 1) * 4;
                const std::size_t subY = (subBlock >> 1) * 4;
                const uint16_t color0 = ReadBigEndian16(source);
                const uint16_t color1 = ReadBigEndian16(source + 2);
                source += 4;

                std::array<Rgba, 4> palette{};
                palette[0] = DecodeRgb565(color0);
                palette[1] = DecodeRgb565(color1);
                if (color0 > color1) {
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        palette[2][channel] = BlendCmpr(palette[0][channel], palette[1][channel]);
                        palette[3][channel] = BlendCmpr(palette[1][channel], palette[0][channel]);
                    }
                    palette[2][3] = palette[3][3] = 255;
                } else {
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        palette[2][channel] = static_cast<uint8_t>((palette[0][channel] + palette[1][channel]) / 2);
                    }
                    palette[2][3] = 255;
                    palette[3] = { palette[2][0], palette[2][1], palette[2][2], 0 };
                }

                for (std::size_t y = 0; y < 4; ++y) {
                    const uint8_t row = *source++;
                    for (std::size_t x = 0; x < 4; ++x) {
                        const uint8_t index = static_cast<uint8_t>((row >> (6 - x * 2)) & 3);
                        const std::size_t pixelX = blockX * 8 + subX + x;
                        const std::size_t pixelY = blockY * 8 + subY + y;
                        if (pixelX < width && pixelY < height) {
                            StorePixel(destination, width, pixelX, pixelY, palette[index]);
                        }
                    }
                }
            }
        }
    }
}

void DecodeC4(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination,
              const Palette& palette) {
    const std::size_t blockWidth = DivideRoundingUp(width, 8);
    const std::size_t blockHeight = DivideRoundingUp(height, 8);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 8; ++y) {
                for (std::size_t x = 0; x < 8; x += 2) {
                    const uint8_t value = *source++;
                    const std::size_t pixelX = blockX * 8 + x;
                    const std::size_t pixelY = blockY * 8 + y;
                    if (pixelX < width && pixelY < height) {
                        StorePixel(destination, width, pixelX, pixelY, palette[value >> 4]);
                    }
                    if (pixelX + 1 < width && pixelY < height) {
                        StorePixel(destination, width, pixelX + 1, pixelY, palette[value & 0x0F]);
                    }
                }
            }
        }
    }
}

void DecodeC8(const uint8_t* source, uint32_t width, uint32_t height, std::span<uint8_t> destination,
              const Palette& palette) {
    const std::size_t blockWidth = DivideRoundingUp(width, 8);
    const std::size_t blockHeight = DivideRoundingUp(height, 4);
    for (std::size_t blockY = 0; blockY < blockHeight; ++blockY) {
        for (std::size_t blockX = 0; blockX < blockWidth; ++blockX) {
            for (std::size_t y = 0; y < 4; ++y) {
                for (std::size_t x = 0; x < 8; ++x) {
                    const uint8_t index = *source++;
                    const std::size_t pixelX = blockX * 8 + x;
                    const std::size_t pixelY = blockY * 4 + y;
                    if (pixelX < width && pixelY < height) {
                        StorePixel(destination, width, pixelX, pixelY, palette[index]);
                    }
                }
            }
        }
    }
}

} // namespace

std::size_t GetEncodedTextureSize(uint32_t width, uint32_t height, TextureFormat format) {
    if (width == 0 || height == 0) {
        return 0;
    }

    BlockLayout layout{};
    if (!GetBlockLayout(format, layout)) {
        return 0;
    }

    const std::size_t blockWidth = DivideRoundingUp(width, layout.width);
    const std::size_t blockHeight = DivideRoundingUp(height, layout.height);
    std::size_t blockCount;
    std::size_t byteCount;
    if (!TryMultiply(blockWidth, blockHeight, blockCount) || !TryMultiply(blockCount, layout.bytes, byteCount)) {
        return 0;
    }
    return byteCount;
}

std::size_t GetDecodedTextureSize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return 0;
    }

    std::size_t pixelCount;
    std::size_t byteCount;
    if (!TryMultiply(width, height, pixelCount) || !TryMultiply(pixelCount, 4, byteCount)) {
        return 0;
    }
    return byteCount;
}

TextureDecodeError DecodeTexture(std::span<const uint8_t> source, uint32_t width, uint32_t height, TextureFormat format,
                                 std::span<uint8_t> destination, const TlutView* tlut) {
    const std::size_t encodedSize = GetEncodedTextureSize(width, height, format);
    const std::size_t decodedSize = GetDecodedTextureSize(width, height);
    if (width == 0 || height == 0 || decodedSize == 0) {
        return TextureDecodeError::InvalidDimensions;
    }
    if (encodedSize == 0) {
        return TextureDecodeError::UnsupportedFormat;
    }
    if (source.size() < encodedSize) {
        return TextureDecodeError::SourceTooSmall;
    }
    if (destination.size() < decodedSize) {
        return TextureDecodeError::DestinationTooSmall;
    }
    const bool indexed = format == TextureFormat::C4 || format == TextureFormat::C8;
    if (indexed && tlut != nullptr && !IsSupportedTlutFormat(tlut->format)) {
        return TextureDecodeError::UnsupportedTlutFormat;
    }
    if (indexed && tlut != nullptr && tlut->entryCount > 0) {
        std::size_t tlutSize;
        if (!TryMultiply(std::min<std::size_t>(tlut->entryCount, 256), 2, tlutSize) || tlut->data.size() < tlutSize) {
            return TextureDecodeError::TlutTooSmall;
        }
    }

    destination = destination.first(decodedSize);
    std::fill(destination.begin(), destination.end(), 0);
    const uint8_t* data = source.data();
    switch (format) {
        case TextureFormat::I4:
            DecodeI4(data, width, height, destination);
            break;
        case TextureFormat::I8:
            DecodeI8(data, width, height, destination);
            break;
        case TextureFormat::IA4:
            DecodeIa4(data, width, height, destination);
            break;
        case TextureFormat::IA8:
            DecodeIa8(data, width, height, destination);
            break;
        case TextureFormat::RGB565:
            DecodeRgb565(data, width, height, destination);
            break;
        case TextureFormat::RGB5A3:
            DecodeRgb5A3(data, width, height, destination);
            break;
        case TextureFormat::RGBA8:
            DecodeRgba8(data, width, height, destination);
            break;
        case TextureFormat::C4:
            DecodeC4(data, width, height, destination, BuildPalette(tlut));
            break;
        case TextureFormat::C8:
            DecodeC8(data, width, height, destination, BuildPalette(tlut));
            break;
        case TextureFormat::CMPR:
            DecodeCmpr(data, width, height, destination);
            break;
        default:
            return TextureDecodeError::UnsupportedFormat;
    }
    return TextureDecodeError::None;
}

} // namespace GX
