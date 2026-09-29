#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "stb/error.hpp"

namespace stb {

class image;

// Abstract base of all image encoders (stb_image_write).
//
//   stb::png_encoder png(6);
//   auto bytes = img.encode(png);           // or img.save("out.png", png)
//
// Uses the non-virtual-interface idiom: encode() is the one public entry
// point and does the argument checks common to every format; subclasses
// only implement do_encode(). Encoders are small immutable value-like
// objects: stateless after construction, so one instance can be shared by
// any number of threads.
class encoder {
public:
    virtual ~encoder() = default;

    // Throws invalid_argument (empty image), limit_error (> 2 GiB), encode_error.
    std::vector<std::uint8_t> encode(const image& img) const;

    // File extension without the dot ("png", "jpg", "bmp", "tga").
    virtual const char* extension() const noexcept = 0;

    // Default-configured encoder for a path's extension (case-insensitive):
    // .png .jpg .jpeg .bmp .tga. Throws invalid_argument for anything else.
    static std::unique_ptr<encoder> for_path(const std::filesystem::path& path);

protected:
    encoder() = default;
    // Protected copy: no slicing through the base.
    encoder(const encoder&) = default;
    encoder& operator=(const encoder&) = default;

private:
    virtual std::vector<std::uint8_t> do_encode(const image& img) const = 0;
};

class png_encoder final : public encoder {
public:
    static constexpr int default_compression = 8;

    // compression in 1..9, higher = smaller and slower (stb treats <5 as 5).
    explicit png_encoder(int compression = default_compression);
    int compression() const noexcept { return compression_; }
    const char* extension() const noexcept override { return "png"; }

private:
    std::vector<std::uint8_t> do_encode(const image& img) const override;
    int compression_;
};

class jpeg_encoder final : public encoder {
public:
    static constexpr int default_quality = 90;

    // quality in 1..100. Alpha, if any, is dropped (JPEG has none).
    explicit jpeg_encoder(int quality = default_quality);
    int quality() const noexcept { return quality_; }
    const char* extension() const noexcept override { return "jpg"; }

private:
    std::vector<std::uint8_t> do_encode(const image& img) const override;
    int quality_;
};

class bmp_encoder final : public encoder {
public:
    const char* extension() const noexcept override { return "bmp"; }

private:
    std::vector<std::uint8_t> do_encode(const image& img) const override;
};

class tga_encoder final : public encoder {
public:
    explicit tga_encoder(bool rle = true) noexcept : rle_(rle) {}
    bool rle() const noexcept { return rle_; }
    const char* extension() const noexcept override { return "tga"; }

private:
    std::vector<std::uint8_t> do_encode(const image& img) const override;
    bool rle_;
};

}  // namespace stb
