#include "common/jpeg_decoder.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#ifdef LIBRESHOCKWAVE_HAVE_JPEG
#include <csetjmp>
#include <jpeglib.h>
#endif

namespace libreshockwave::apps {
namespace {

#ifdef LIBRESHOCKWAVE_HAVE_JPEG

struct JpegErrorManager {
    jpeg_error_mgr public_error;
    std::jmp_buf jump;
};

void jpegErrorExit(j_common_ptr common) {
    auto* error = reinterpret_cast<JpegErrorManager*>(common->err);
    longjmp(error->jump, 1);
}

#endif

} // namespace

std::optional<bitmap::Bitmap> decodeJpeg(const std::vector<std::uint8_t>& data) {
#ifdef LIBRESHOCKWAVE_HAVE_JPEG
    if (data.empty() || data.size() > std::numeric_limits<unsigned long>::max()) {
        return std::nullopt;
    }

    jpeg_decompress_struct decoder{};
    JpegErrorManager error{};
    bool created = false;
    decoder.err = jpeg_std_error(&error.public_error);
    error.public_error.error_exit = jpegErrorExit;
    if (setjmp(error.jump) != 0) {
        if (created) {
            jpeg_destroy_decompress(&decoder);
        }
        return std::nullopt;
    }

    jpeg_create_decompress(&decoder);
    created = true;
    jpeg_mem_src(&decoder,
                 const_cast<unsigned char*>(data.data()),
                 static_cast<unsigned long>(data.size()));
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&decoder);
        return std::nullopt;
    }
    decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&decoder);

    const auto width = static_cast<int>(decoder.output_width);
    const auto height = static_cast<int>(decoder.output_height);
    if (width <= 0 || height <= 0 || decoder.output_components != 3) {
        jpeg_destroy_decompress(&decoder);
        return std::nullopt;
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));
    std::vector<JSAMPLE> scanline(static_cast<std::size_t>(width) * 3U);
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row = scanline.data();
        const auto y = decoder.output_scanline;
        jpeg_read_scanlines(&decoder, &row, 1);
        for (int x = 0; x < width; ++x) {
            const auto offset = static_cast<std::size_t>(x) * 3U;
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] =
                0xFF000000U |
                (static_cast<std::uint32_t>(scanline[offset]) << 16U) |
                (static_cast<std::uint32_t>(scanline[offset + 1U]) << 8U) |
                static_cast<std::uint32_t>(scanline[offset + 2U]);
        }
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    return bitmap::Bitmap(width, height, 32, std::move(pixels));
#else
    (void)data;
    return std::nullopt;
#endif
}

} // namespace libreshockwave::apps
