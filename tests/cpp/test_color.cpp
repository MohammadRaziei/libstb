#include <sstream>
#include <string>

#include "test_common.hpp"

using namespace stb;
using namespace stb::color_literals;

UTEST(libstb_color_tests, test_default_constructor) {
    color c;
    ASSERT_TRUE(c.r() == 0);
    ASSERT_TRUE(c.g() == 0);
    ASSERT_TRUE(c.b() == 0);
    ASSERT_TRUE(c.a() == 255);
}

UTEST(libstb_color_tests, test_rgba_constructor) {
    color c(255, 0, 0, 255);
    ASSERT_TRUE(c.r() == 255);
    ASSERT_TRUE(c.g() == 0);
    ASSERT_TRUE(c.b() == 0);
    ASSERT_TRUE(c.a() == 255);

    color c2(0, 255, 0, 128);
    ASSERT_TRUE(c2.r() == 0);
    ASSERT_TRUE(c2.g() == 255);
    ASSERT_TRUE(c2.b() == 0);
    ASSERT_TRUE(c2.a() == 128);
}

UTEST(libstb_color_tests, test_setters) {
    color c;
    c.set_r(100);
    c.set_g(150);
    c.set_b(200);
    c.set_a(50);

    ASSERT_TRUE(c.r() == 100);
    ASSERT_TRUE(c.g() == 150);
    ASSERT_TRUE(c.b() == 200);
    ASSERT_TRUE(c.a() == 50);
}

UTEST(libstb_color_tests, test_value) {
    color c(255, 0, 0, 255);
    ASSERT_TRUE(c.value() == 0xFF0000FF);

    color c2(0, 255, 0, 128);
    ASSERT_TRUE(c2.value() == 0x00FF0080);

    color c3(0, 0, 0, 0);
    ASSERT_TRUE(c3.value() == 0x00000000);

    color c4(255, 255, 255, 255);
    ASSERT_TRUE(c4.value() == 0xFFFFFFFF);
}

UTEST(libstb_color_tests, test_tostring) {
    color c(255, 0, 0, 255);
    ASSERT_TRUE(c.to_string() == "color(r=255, g=0, b=0, a=255)");
}

UTEST(libstb_color_tests, test_implicit_string_conversion) {
    color c(100, 150, 200, 50);
    std::string s = c;
    ASSERT_TRUE(s == "color(r=100, g=150, b=200, a=50)");
}

UTEST(libstb_color_tests, test_fromhash) {
    {
        color c = color::from_hash("#FF0000");
        ASSERT_TRUE(c.r() == 255);
        ASSERT_TRUE(c.g() == 0);
        ASSERT_TRUE(c.b() == 0);
        ASSERT_TRUE(c.a() == 255);
    }

    {
        color c = color::from_hash("#F00");
        ASSERT_TRUE(c.r() == 255);
        ASSERT_TRUE(c.g() == 0);
        ASSERT_TRUE(c.b() == 0);
        ASSERT_TRUE(c.a() == 255);
    }

    {
        color c = color::from_hash("#FF000080");
        ASSERT_TRUE(c.r() == 255);
        ASSERT_TRUE(c.g() == 0);
        ASSERT_TRUE(c.b() == 0);
        ASSERT_TRUE(c.a() == 128);
    }

    {
        color c = color::from_hash("#F008");
        ASSERT_TRUE(c.r() == 255);
        ASSERT_TRUE(c.g() == 0);
        ASSERT_TRUE(c.b() == 0);
        ASSERT_TRUE(c.a() == 136);  // 0x08 * 0x11 = 136
    }

    {
        ASSERT_THROWS(color::from_hash("FF0000"), std::invalid_argument);
        ASSERT_THROWS(color::from_hash("#GG0000"), std::invalid_argument);
        ASSERT_THROWS(color::from_hash("#FF000"), std::invalid_argument);
        ASSERT_THROWS(color::from_hash("#12345g"), std::invalid_argument);  // used to parse as valid
        ASSERT_THROWS(color::from_hash("#12345g"), invalid_color_error);
        try {
            color::from_hash("#gggggg");
            ASSERT_TRUE(false);
        } catch (const std::invalid_argument& e) {
            ASSERT_STREQ("Invalid hex color '#gggggg': 'g' is not a hex digit (0-9, a-f, A-F)", e.what());
        }
    }
}

UTEST(libstb_color_tests, test_fromname) {
    ASSERT_TRUE(color::from_name("red") == color::red());
    ASSERT_TRUE(color::from_name("blue") == color::blue());
    ASSERT_TRUE(color::from_name("green") == color::green());
    ASSERT_TRUE(color::from_name("RED") == color::red());      // Case insensitive
    ASSERT_TRUE(color::from_name("Blue") == color::blue());   // Case insensitive
    ASSERT_TRUE(color::from_name("grey") == color::gray());    // British spelling

    ASSERT_THROWS(color::from_name("invalid_color"), std::invalid_argument);
    ASSERT_THROWS(color::from_name(""), std::invalid_argument);
}

UTEST(libstb_color_tests, test_fromstring) {
    ASSERT_TRUE(color::from_string("#FF0000") == color::red());
    ASSERT_TRUE(color::from_string("blue") == color::blue());
    ASSERT_TRUE(color::from_string("#F00") == color::red());

    ASSERT_THROWS(color::from_string(""), std::invalid_argument);
}

UTEST(libstb_color_tests, test_fromvalue) {
    color c = color::from_value(0xFF0000FF);
    ASSERT_TRUE(c.r() == 255);
    ASSERT_TRUE(c.g() == 0);
    ASSERT_TRUE(c.b() == 0);
    ASSERT_TRUE(c.a() == 255);

    color c2 = color::from_value(0x00000000);
    ASSERT_TRUE(c2.r() == 0);
    ASSERT_TRUE(c2.g() == 0);
    ASSERT_TRUE(c2.b() == 0);
    ASSERT_TRUE(c2.a() == 0);

    color c3 = color::from_value(0xAABBCCDD);
    ASSERT_TRUE(c3.r() == 0xAA);
    ASSERT_TRUE(c3.g() == 0xBB);
    ASSERT_TRUE(c3.b() == 0xCC);
    ASSERT_TRUE(c3.a() == 0xDD);
}

UTEST(libstb_color_tests, test_predefined_colors) {
    ASSERT_TRUE(color::black() == color(0, 0, 0));
    ASSERT_TRUE(color::white() == color(255, 255, 255));
    ASSERT_TRUE(color::red() == color(255, 0, 0));
    ASSERT_TRUE(color::green() == color(0, 128, 0));
    ASSERT_TRUE(color::lime() == color(0, 255, 0));
    ASSERT_TRUE(color::blue() == color(0, 0, 255));
    ASSERT_TRUE(color::yellow() == color(255, 255, 0));
    ASSERT_TRUE(color::cyan() == color(0, 255, 255));
    ASSERT_TRUE(color::magenta() == color(255, 0, 255));
    ASSERT_TRUE(color::silver() == color(192, 192, 192));
    ASSERT_TRUE(color::gray() == color(128, 128, 128));
    ASSERT_TRUE(color::maroon() == color(128, 0, 0));
    ASSERT_TRUE(color::olive() == color(128, 128, 0));
    ASSERT_TRUE(color::purple() == color(128, 0, 128));
    ASSERT_TRUE(color::teal() == color(0, 128, 128));
    ASSERT_TRUE(color::navy() == color(0, 0, 128));
    ASSERT_TRUE(color::orange() == color(255, 165, 0));
    ASSERT_TRUE(color::pink() == color(255, 192, 203));
    ASSERT_TRUE(color::brown() == color(165, 42, 42));
    ASSERT_TRUE(color::transparent() == color(0, 0, 0, 0));
}

UTEST(libstb_color_tests, test_blend_with_transparent_int) {
    color gray = color::gray();  // (128, 128, 128, 255)
    color transparent = color::transparent();  // (0, 0, 0, 0)

    {
        color result = gray | 0;
        ASSERT_TRUE(result.r() == 128);
        ASSERT_TRUE(result.g() == 128);
        ASSERT_TRUE(result.b() == 128);
        ASSERT_TRUE(result.a() == 255);
    }

    {
        color result = gray | 50;
        ASSERT_TRUE(result.r() == 64);   // 128 * 0.5
        ASSERT_TRUE(result.g() == 64);
        ASSERT_TRUE(result.b() == 64);
        ASSERT_TRUE(result.a() == 128);  // 255 * 0.5
    }

    {
        color result = gray | 100;
        ASSERT_TRUE(result.r() == 0);
        ASSERT_TRUE(result.g() == 0);
        ASSERT_TRUE(result.b() == 0);
        ASSERT_TRUE(result.a() == 0);
    }

    {
        color result = gray | -50;
        ASSERT_TRUE(result == gray);
    }

    {
        color result = gray | 150;
        ASSERT_TRUE(result == transparent);
    }
}

UTEST(libstb_color_tests, test_blend_with_another_color_color) {
    color red = color::red();    // (255, 0, 0)
    color blue = color::blue();   // (0, 0, 255)

    color result = red | blue;  // 50% each

    ASSERT_TRUE(result.r() == 128);  // 255 * 0.5 + 0 * 0.5
    ASSERT_TRUE(result.g() == 0);
    ASSERT_TRUE(result.b() == 128);  // 0 * 0.5 + 255 * 0.5
    ASSERT_TRUE(result.a() == 255);
}

UTEST(libstb_color_tests, test_blend_method) {
    color red = color::red();
    color blue = color::blue();

    {
        color result = red.blend(blue, 0);
        ASSERT_TRUE(result == red);
    }

    {
        color result = red.blend(blue, 25);
        ASSERT_TRUE(result.r() == 192);  // 255 * 0.75 + 0 * 0.25
        ASSERT_TRUE(result.b() == 64);   // 0 * 0.75 + 255 * 0.25
    }

    {
        color result = red.blend(blue, 100);
        ASSERT_TRUE(result == blue);
    }
}

UTEST(libstb_color_tests, test_colorblend_chaining) {
    {
        // 50% gray + 50% black
        color result = "gray"_c | 50 | "black"_c;
        
        // gray = (128, 128, 128), black = (0, 0, 0)
        ASSERT_TRUE(result.r() == 64);
        ASSERT_TRUE(result.g() == 64);
        ASSERT_TRUE(result.b() == 64);
    }

    {
        // red = (255, 0, 0), blue = (0, 0, 255)
        color result = "red"_c | 50 | "blue"_c;
        
        ASSERT_TRUE(result.r() == 128);
        ASSERT_TRUE(result.g() == 0);
        ASSERT_TRUE(result.b() == 128);
    }

    {
        // red = (255, 0, 0), blue = (0, 0, 255)
        color result = "red"_c | 30 | "blue"_c;
        
        ASSERT_TRUE(result.r() == 179);
        ASSERT_TRUE(result.g() == 0);
        ASSERT_TRUE(result.b() == 77);
    }
}

UTEST(libstb_color_tests, test_string_constructor) {
    color c1("#FF0000");
    ASSERT_TRUE(c1 == color::red());

    color c2("blue");
    ASSERT_TRUE(c2 == color::blue());

    color c3("#F00");
    ASSERT_TRUE(c3 == color::red());

    ASSERT_THROWS(color("invalid"), std::invalid_argument);
}

UTEST(libstb_color_tests, test_literal_operator) {
    ASSERT_TRUE("red"_c == color::red());
    ASSERT_TRUE("blue"_c == color::blue());
    ASSERT_TRUE("#FF0000"_c == color::red());
    ASSERT_TRUE("#F00"_c == color::red());
    ASSERT_TRUE("Gray"_c == color::gray());  // Case insensitive
}

UTEST(libstb_color_tests, test_stream_output) {
    std::ostringstream oss;
    oss << color::red();
    ASSERT_TRUE(oss.str() == "color(r=255, g=0, b=0, a=255)");

    oss.str("");
    oss << color_blend(color::blue(), 50);
    ASSERT_TRUE(oss.str() == "color(r=0, g=0, b=128, a=128)");
}

UTEST(libstb_color_tests, test_rgba_factory) {
    color c = color::rgba(100, 150, 200, 50);
    ASSERT_TRUE(c.r() == 100);
    ASSERT_TRUE(c.g() == 150);
    ASSERT_TRUE(c.b() == 200);
    ASSERT_TRUE(c.a() == 50);

    color c2 = color::rgba(255, 0, 0);
    ASSERT_TRUE(c2.a() == 255);  // Default alpha
}

UTEST(libstb_color_tests, test_roundtrip) {
    {
        color original(100, 150, 200, 50);
        uint32_t val = original.value();
        color restored = color::from_value(val);
        
        ASSERT_TRUE(restored.r() == original.r());
        ASSERT_TRUE(restored.g() == original.g());
        ASSERT_TRUE(restored.b() == original.b());
        ASSERT_TRUE(restored.a() == original.a());
    }

    {
        color original(100, 150, 200, 50);
        std::string s = original.to_string();
        ASSERT_TRUE(s.find("100") != std::string::npos);
        ASSERT_TRUE(s.find("150") != std::string::npos);
        ASSERT_TRUE(s.find("200") != std::string::npos);
        ASSERT_TRUE(s.find("50") != std::string::npos);
    }
}
static int red_of(const color& c) { return c.r(); }

UTEST(libstb_color_tests, test_implicit_conversions) {
    // string literal and braced rgb(a) convert without naming the type
    ASSERT_TRUE(red_of("red") == 255);
    ASSERT_TRUE(red_of({10, 20, 30}) == 10);
    ASSERT_TRUE(red_of({10, 20, 30, 40}) == 10);
    color c = {1, 2, 3};
    ASSERT_TRUE(c.b() == 3 && c.a() == 255);
    color h = "#ff8800";
    ASSERT_TRUE(h.r() == 255 && h.g() == 136 && h.b() == 0);
}

UTEST(libstb_color_tests, test_from_rgb_and_rgb_literal) {
    color a = color::from_rgb(0x030303);
    ASSERT_TRUE(a.r() == 3 && a.g() == 3 && a.b() == 3 && a.a() == 255);
    color b = color::from_rgb(0xFF8800);
    ASSERT_TRUE(b.r() == 255 && b.g() == 136 && b.b() == 0 && b.a() == 255);
    color c = 0x030303_rgb;
    ASSERT_TRUE(c.r() == 3 && c.b() == 3 && c.a() == 255);
    ASSERT_TRUE((0x00112233_rgb).b() == 0x33);
    ASSERT_TRUE(color::from_rgb(0).a() == 255);
}

UTEST(libstb_color_tests, test_from_rgb_rejects_rgba_values) {
    bool threw = false;
    try {
        (void)color::from_rgb(0xFF0000FFu);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
    threw = false;
    try {
        (void)(0x1000000_rgb);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}
