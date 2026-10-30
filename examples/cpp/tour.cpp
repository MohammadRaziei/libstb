// A tour of the C++ API: load, inspect, geometry, resize, blend, fill, masks, stacking, canvas, encode.
//
//   tour photo.jpg font.ttf outdir/
#include <filesystem>
#include <iostream>
#include <utility>

#include "stb.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: " << argv[0] << " <image> <font.ttf> <outdir>\n";
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path out_dir = argv[3];
    fs::create_directories(out_dir);

    try {
        // 1. Inspect without decoding any pixel.
        const stb::image_info info = stb::image_info::read_file(argv[1]);
        std::cout << info.width << "x" << info.height << " " << stb::pixel_format_name(info.format) << '\n';

        // 2. Load. Options are optional: here, upright (EXIF) and always RGB.
        stb::load_options opt;
        opt.orient = true;
        opt.format = stb::pixel_format::rgb;
        stb::image photo = stb::image::open(argv[1], opt);

        // 3. Geometry: every call returns a new image. A temporary is moved, not copied.
        stb::image turned = photo.rotate90(1);                // 1 quarter turn counter-clockwise
        stb::image mirrored = photo.flip_horizontal();
        stb::image piece = photo.crop(0, 0, photo.width() / 2, photo.height() / 2);
        stb::image framed = photo.pad(20, 20, 20, 20, {255, 255, 255});  // white border

        // 4. Resize: by filter name, by enum, or with a reusable resizer (shareable between threads).
        stb::image small = photo.resize(320, 240, "cubic");
        stb::image thumb = photo.thumbnail(200, 200, stb::resize_filter::mitchell);
        const stb::resizer sharp(stb::resize_filter::catmull_rom);
        stb::image big = small.resize(1280, 960, &sharp);

        // 5. Put images into an image. blend() and fill() change the image they are called on:
        //    copy() first to keep the original. Every call returns the image, so they chain.
        stb::image badge = photo.thumbnail(160, 160);
        stb::image shaded = photo.copy()
                                .fill({0, 0, 0}, 0, photo.height() - 60, photo.width(), 60)       // a black strip
                                .blend(badge, 16, 16)                                              // normal "over"
                                .blend(badge, 200, 16, stb::blend_mode::multiply)                  // a photoshop mode
                                .blend(badge, 400, 16, stb::blend_mode::screen, 0.5f);             // and half opacity

        // A mask is just a gray image: 255 shows the overlay, 0 hides it (here a left-to-right fade).
        stb::image mask(badge.width(), badge.height(), stb::pixel_format::gray);
        for (int y = 0; y < mask.height(); ++y)
            for (int x = 0; x < mask.width(); ++x) mask(x, y) = std::uint8_t(x * 255 / (mask.width() - 1));
        stb::image faded = photo.copy().blend(badge.convert(stb::pixel_format::rgb).with_alpha(mask), 16, 16);
        stb::image cutout_mask = badge.convert(stb::pixel_format::rgba).alpha();  // alpha() gives a mask back

        // Side by side or one above the other, with a gap, a background and alignment.
        stb::image row = stb::image::hstack({&badge, &faded, &piece}, 8, "white", stb::align::center);
        stb::image sheet = stb::image::vstack({&row, &shaded}, 8, "white");

        // 6. Canvas: shapes, texts and images collected on ONE copy of the photo, drawn in order by finish().
        stb::font f = stb::font::open(argv[2]);
        stb::canvas c(photo);
        c.set_font(f, 48).set_color("white");

        c.add_rect(10, 10, 360, 70).fill({0, 0, 0, 140}).radius(12).stroke(2, "white");
        c.add_text("Hello, libstb", 24, 18);
        c.add_ellipse(photo.width() - 90, 20, 70, 70).fill("#ffcc00");
        c.add_line(10, 92, 370, 92).stroke(3, "#ffcc00");
        c.add_image(badge, photo.width() - 180, photo.height() - 180).blend(stb::blend_mode::multiply).opacity(0.8f);

        const int mid = photo.width() / 2;
        c.add_text("first line\nsecond, longer line", mid, photo.height() - 150).align(stb::text_align::center);
        c.add_text("watermark", mid, photo.height() - 50)
            .align(stb::text_align::center)
            .color({255, 255, 255, 120})
            .bg_color({0, 0, 0, 90})
            .padding(6)
            .size(24);

        stb::image labeled = c.finish();  // c is finished now: no more add_*

        // 7. Rotate the finished result and save. Moving the temporary: rotate90 reuses nothing wasted.
        stb::image final_image = std::move(labeled).rotate90(-1);  // one turn clockwise

        // 8. Pixel access and channels.
        final_image(0, 0, 0) = 255;                                  // red of the top-left pixel
        stb::image rgba = final_image.convert(stb::pixel_format::rgba);
        stb::image gray = final_image.convert(stb::pixel_format::gray);
        std::vector<stb::image> planes = final_image.split();        // R, G, B as gray images

        // 9. Encode: the format follows the extension, or call the write_* you want with its settings.
        final_image.write(out_dir / "final.png");
        final_image.write_jpg(out_dir / "final.jpg", 85);
        gray.write_png(out_dir / "gray.png", 9);
        std::vector<std::uint8_t> bytes = thumb.to_png();           // in memory, e.g. for a socket
        std::cout << "thumb.png: " << bytes.size() << " bytes, planes: " << planes.size()
                  << ", rgba has alpha: " << stb::has_alpha(rgba.format()) << '\n';

        turned.write(out_dir / "turned.png");
        mirrored.write(out_dir / "mirrored.png");
        sheet.write(out_dir / "sheet.png");
        cutout_mask.write(out_dir / "mask.png");
        piece.write(out_dir / "piece.png");
        framed.write(out_dir / "framed.png");
        big.write(out_dir / "big.png");
    } catch (const stb::decode_error& e) {
        std::cerr << "not a valid image or font: " << e.what() << '\n';
        return 1;
    } catch (const stb::error& e) {  // io_error, limit_error, encode_error
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {  // invalid_argument and the like
        std::cerr << "bad call: " << e.what() << '\n';
        return 1;
    }
}
