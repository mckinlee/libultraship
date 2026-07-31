#include <gtest/gtest.h>

#include "gx/texture/TextureDecoder.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

std::array<uint8_t, 4> Pixel(const std::vector<uint8_t>& rgba8, std::size_t index = 0) {
    const std::size_t offset = index * 4;
    return { rgba8[offset], rgba8[offset + 1], rgba8[offset + 2], rgba8[offset + 3] };
}

std::vector<uint8_t> Decode(std::vector<uint8_t> source, uint32_t width, uint32_t height, GX::TextureFormat format,
                            const GX::TlutView* tlut = nullptr) {
    std::vector<uint8_t> destination(GX::GetDecodedTextureSize(width, height));
    EXPECT_EQ(GX::DecodeTexture(source, width, height, format, destination, tlut), GX::TextureDecodeError::None);
    return destination;
}

} // namespace

TEST(GxTextureDecoder, ReportsTiledSourceAndLinearDestinationSizes) {
    EXPECT_EQ(GX::GetEncodedTextureSize(8, 8, GX::TextureFormat::I4), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(9, 8, GX::TextureFormat::I4), 64);
    EXPECT_EQ(GX::GetEncodedTextureSize(8, 4, GX::TextureFormat::I8), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(8, 4, GX::TextureFormat::IA4), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(4, 4, GX::TextureFormat::IA8), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(4, 4, GX::TextureFormat::RGB565), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(4, 4, GX::TextureFormat::RGB5A3), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(4, 4, GX::TextureFormat::RGBA8), 64);
    EXPECT_EQ(GX::GetEncodedTextureSize(8, 8, GX::TextureFormat::C4), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(8, 4, GX::TextureFormat::C8), 32);
    EXPECT_EQ(GX::GetEncodedTextureSize(8, 8, GX::TextureFormat::CMPR), 32);
    EXPECT_EQ(GX::GetDecodedTextureSize(3, 2), 24);
    EXPECT_EQ(GX::GetEncodedTextureSize(UINT32_MAX, UINT32_MAX, GX::TextureFormat::RGBA8), 0);
    EXPECT_EQ(GX::GetDecodedTextureSize(UINT32_MAX, UINT32_MAX), 0);
    if constexpr (sizeof(std::size_t) >= sizeof(uint64_t)) {
        EXPECT_EQ(GX::GetEncodedTextureSize(UINT32_MAX, 1, GX::TextureFormat::I4), 17179869184ULL);
        EXPECT_EQ(GX::GetDecodedTextureSize(UINT32_MAX, 1), 17179869180ULL);
    }
}

TEST(GxTextureDecoder, DecodesIntensityFormats) {
    std::vector<uint8_t> i4(32);
    i4[0] = 0x1F;
    const auto decodedI4 = Decode(i4, 2, 1, GX::TextureFormat::I4);
    EXPECT_EQ(Pixel(decodedI4, 0), (std::array<uint8_t, 4>{ 0x11, 0x11, 0x11, 0x11 }));
    EXPECT_EQ(Pixel(decodedI4, 1), (std::array<uint8_t, 4>{ 0xFF, 0xFF, 0xFF, 0xFF }));

    std::vector<uint8_t> i8(32);
    i8[0] = 0x7B;
    const auto decodedI8 = Decode(i8, 1, 1, GX::TextureFormat::I8);
    EXPECT_EQ(Pixel(decodedI8), (std::array<uint8_t, 4>{ 0x7B, 0x7B, 0x7B, 0x7B }));
}

TEST(GxTextureDecoder, DecodesIntensityAlphaFormats) {
    std::vector<uint8_t> ia4(32);
    ia4[0] = 0xA3;
    const auto decodedIa4 = Decode(ia4, 1, 1, GX::TextureFormat::IA4);
    EXPECT_EQ(Pixel(decodedIa4), (std::array<uint8_t, 4>{ 0x33, 0x33, 0x33, 0xAA }));

    std::vector<uint8_t> ia8(32);
    ia8[0] = 0x44;
    ia8[1] = 0xCC;
    const auto decodedIa8 = Decode(ia8, 1, 1, GX::TextureFormat::IA8);
    EXPECT_EQ(Pixel(decodedIa8), (std::array<uint8_t, 4>{ 0xCC, 0xCC, 0xCC, 0x44 }));
}

TEST(GxTextureDecoder, DecodesRgb565) {
    std::vector<uint8_t> source(32);
    source[0] = 0x20;
    source[1] = 0x00;
    const auto decoded = Decode(source, 1, 1, GX::TextureFormat::RGB565);
    EXPECT_EQ(Pixel(decoded), (std::array<uint8_t, 4>{ 33, 0, 0, 255 }));
}

TEST(GxTextureDecoder, DecodesBothRgb5A3EncodingsWithBitReplication) {
    std::vector<uint8_t> source(32);
    source[0] = 0xFF;
    source[1] = 0xFF;
    source[2] = 0x71;
    source[3] = 0x23;
    const auto decoded = Decode(source, 2, 1, GX::TextureFormat::RGB5A3);
    EXPECT_EQ(Pixel(decoded, 0), (std::array<uint8_t, 4>{ 255, 255, 255, 255 }));
    EXPECT_EQ(Pixel(decoded, 1), (std::array<uint8_t, 4>{ 0x11, 0x22, 0x33, 255 }));
}

TEST(GxTextureDecoder, DecodesPlanarRgba8) {
    std::vector<uint8_t> source(64);
    source[0] = 0x11;
    source[1] = 0x22;
    source[32] = 0x33;
    source[33] = 0x44;
    const auto decoded = Decode(source, 1, 1, GX::TextureFormat::RGBA8);
    EXPECT_EQ(Pixel(decoded), (std::array<uint8_t, 4>{ 0x22, 0x33, 0x44, 0x11 }));
}

TEST(GxTextureDecoder, DecodesC4WithBigEndianRgb5A3Tlut) {
    std::vector<uint8_t> source(32);
    source[0] = 0x01;
    std::array<uint8_t, 32> palette{};
    palette[0] = 0xFC;
    palette[1] = 0x00;
    palette[2] = 0x83;
    palette[3] = 0xE0;
    const GX::TlutView tlut{ palette, GX::TlutFormat::RGB5A3, 16, GX::TlutByteOrder::BigEndian };
    const auto decoded = Decode(source, 2, 1, GX::TextureFormat::C4, &tlut);
    EXPECT_EQ(Pixel(decoded, 0), (std::array<uint8_t, 4>{ 255, 0, 0, 255 }));
    EXPECT_EQ(Pixel(decoded, 1), (std::array<uint8_t, 4>{ 0, 255, 0, 255 }));
}

TEST(GxTextureDecoder, DecodesC8WithNativeEndianIa8Tlut) {
    std::vector<uint8_t> source(32);
    source[0] = 1;
    std::array<uint16_t, 2> palette{ 0, 0xA1B2 };
    const auto bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(palette.data()), sizeof(palette));
    const GX::TlutView tlut{ bytes, GX::TlutFormat::IA8, palette.size(), GX::TlutByteOrder::NativeEndian };
    const auto decoded = Decode(source, 1, 1, GX::TextureFormat::C8, &tlut);
    EXPECT_EQ(Pixel(decoded), (std::array<uint8_t, 4>{ 0xB2, 0xB2, 0xB2, 0xA1 }));
}

TEST(GxTextureDecoder, DecodesC8WithBigEndianIa8Tlut) {
    std::vector<uint8_t> source(32);
    std::array<uint8_t, 2> palette{ 0xA1, 0xB2 };
    const GX::TlutView tlut{ palette, GX::TlutFormat::IA8, 1, GX::TlutByteOrder::BigEndian };
    const auto decoded = Decode(source, 1, 1, GX::TextureFormat::C8, &tlut);
    EXPECT_EQ(Pixel(decoded), (std::array<uint8_t, 4>{ 0xB2, 0xB2, 0xB2, 0xA1 }));
}

TEST(GxTextureDecoder, DecodesRgb565Tlut) {
    std::vector<uint8_t> source(32);
    std::array<uint8_t, 2> palette{ 0x00, 0x1F };
    const GX::TlutView tlut{ palette, GX::TlutFormat::RGB565, 1, GX::TlutByteOrder::BigEndian };
    const auto decoded = Decode(source, 1, 1, GX::TextureFormat::C8, &tlut);
    EXPECT_EQ(Pixel(decoded), (std::array<uint8_t, 4>{ 0, 0, 255, 255 }));
}

TEST(GxTextureDecoder, RequiresTlutEntriesForIndexedTextures) {
    std::vector<uint8_t> source(32);
    std::vector<uint8_t> destination(4, 0xFF);
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::C4, destination), GX::TextureDecodeError::MissingTlut);

    const GX::TlutView emptyTlut{};
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::C8, destination, &emptyTlut),
              GX::TextureDecodeError::MissingTlut);

    std::array<uint8_t, 2> palette{};
    const GX::TlutView tlut{ palette, GX::TlutFormat::IA8, 1, GX::TlutByteOrder::BigEndian };
    source[0] = 0x10;
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::C4, destination, &tlut),
              GX::TextureDecodeError::TlutIndexOutOfRange);
    EXPECT_EQ(destination, (std::vector<uint8_t>{ 0, 0, 0, 0 }));

    source[0] = 1;
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::C8, destination, &tlut),
              GX::TextureDecodeError::TlutIndexOutOfRange);

    source.assign(32, 0xFF);
    source[0] = 0x0F;
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::C4, destination, &tlut), GX::TextureDecodeError::None);
}

TEST(GxTextureDecoder, DecodesCmprSubBlocks) {
    std::vector<uint8_t> source(32);
    source[0] = 0xF8;
    source[1] = 0x00;
    source[4] = 0x80;
    const auto decoded = Decode(source, 1, 1, GX::TextureFormat::CMPR);
    EXPECT_EQ(Pixel(decoded), (std::array<uint8_t, 4>{ 159, 0, 0, 255 }));

    source.assign(32, 0);
    source[2] = 0xFF;
    source[3] = 0xFF;
    source[4] = 0xC0;
    const auto transparent = Decode(source, 1, 1, GX::TextureFormat::CMPR);
    EXPECT_EQ(Pixel(transparent), (std::array<uint8_t, 4>{ 127, 127, 127, 0 }));
}

TEST(GxTextureDecoder, ReadsPartialEdgeTilesInBlockOrder) {
    std::vector<uint8_t> source(64);
    source[32] = 0xA0;
    const auto decoded = Decode(source, 9, 1, GX::TextureFormat::I4);
    EXPECT_EQ(Pixel(decoded, 8), (std::array<uint8_t, 4>{ 0xAA, 0xAA, 0xAA, 0xAA }));
}

TEST(GxTextureDecoder, RejectsInvalidBuffersAndFormats) {
    std::vector<uint8_t> source(32);
    std::vector<uint8_t> destination(4);
    EXPECT_EQ(GX::DecodeTexture(source, 0, 1, GX::TextureFormat::I4, destination),
              GX::TextureDecodeError::InvalidDimensions);
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, static_cast<GX::TextureFormat>(7), destination),
              GX::TextureDecodeError::UnsupportedFormat);
    EXPECT_EQ(GX::DecodeTexture(std::span<const uint8_t>(source).first(31), 1, 1, GX::TextureFormat::I4, destination),
              GX::TextureDecodeError::SourceTooSmall);
    EXPECT_EQ(GX::DecodeTexture(source, 2, 1, GX::TextureFormat::I4, destination),
              GX::TextureDecodeError::DestinationTooSmall);

    const std::array<uint8_t, 1> shortPalette{};
    const GX::TlutView tlut{ shortPalette, GX::TlutFormat::IA8, 1, GX::TlutByteOrder::BigEndian };
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::C4, destination, &tlut),
              GX::TextureDecodeError::TlutTooSmall);

    const GX::TlutView unsupportedTlut{ {}, static_cast<GX::TlutFormat>(3), 0, GX::TlutByteOrder::BigEndian };
    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::C4, destination, &unsupportedTlut),
              GX::TextureDecodeError::UnsupportedTlutFormat);

    EXPECT_EQ(GX::DecodeTexture(source, 1, 1, GX::TextureFormat::I4, destination, &tlut), GX::TextureDecodeError::None);
}
