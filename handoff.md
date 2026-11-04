# handoff.md (وضعیت پروژه برای ادامه‌ی کار در سشن بعدی)

تاریخ: 2026-11-02. ریپو: mohammadraziei/libstb (از 0.9.0، کامیت `48bfda8`). این فایل untracked است
(قبل از push حذفش کن). کارهای جدول زیر commit شده‌اند؛ تغییرات «بازنامگذاری 2026-11-04» پایین‌تر هنوز commit نشده‌اند (working tree عمداً
dirty است؛ قبل از commit با `git status` ببین).

## کامیت‌های این سشن (به ترتیب)

| کامیت | چه شد |
|---|---|
| `0f3c5d0` | issue #1: `stb::rgba_to_premultiplied_argb` / `premultiplied_argb_to_rgba` (کرنل‌های SIMD، `src/image/kernels.hpp`، `ops.cpp`) |
| `685f126` | threading هوشمند: `stb::thread_count / set_threads / thread_scope`، پایتون `threads(n)`، env `LIBSTB_THREADS`، `src/util/parallel.hpp`، `src/util/threads.cpp`؛ فقط `resize` ازش استفاده می‌کند |
| `8e76e18` | `stb::sformat / vsformat / sformat_to` در `include/stb/util.hpp`؛ پیام‌های خطای دستی به آن‌ها تبدیل شدند |
| `657ae77` | API گلیف-ایندکس `stb::font` (بخش الف درخواست novasvg) |

## بازنامگذاری 2026-11-04 (commit نشده)

هدف: `path` فقط به معنی مسیر فایل بماند. همه با CMake بیلد و تست شد (C++ پاس، پایتون 1394 پاس و 4 skip).

- هندسه‌ی برداری: `stb::path` شد `stb::outline` (`include/stb/outline.hpp`، `src/draw/outline.cpp`)، همراه `outline_op`،
  `outline_command`، `outline_item`، `canvas::add_outline`. پایتون: `Outline`، `CanvasOutline`، `Image.fill_outline` و
  `stroke_outline`، `Canvas.add_outline`. نوع داخلی ثابت‌نقطه‌ی قبلی شد `detail::fixed_outline` (`src/draw/raster/fixed_outline.hpp`).
  این نام‌ها فقط در بخش Unreleased بودند، پس چیزی از نسخه‌ی منتشرشده نمی‌شکند.
- `include/stb/shaping.hpp` شد `include/stb/rtl.hpp` (و `src/font/shaping.cpp` شد `rtl.cpp`)؛ توابع همان‌اند. این **یک
  شکست API نسبت به 0.9.0** است (فقط نام هدر؛ `stb.h` همان را include می‌کند).
- ساختار فایل‌ها: انکودرها و deflate رفتند به `src/codec/encode/`؛ `src/font/truetype/raster.cpp` شد `glyph_raster.cpp`؛
  `bind_raster.cpp` شد `bind_outline.cpp`؛ `src/image/resize/simd.hpp` شد `simd_vec.hpp`.
- **عمداً دست نخورد:** `stb::shape`، `draw_shape`، `shape_item`، پایتون `CanvasShape` (API منتشرشده؛ اگر قرار است عوض شود، کاربر
  بگوید: پیشنهاد قبلی `draw_primitive`). `include/stb/export.hpp` هم که `STB_MIN/STB_MAX` را دارد جابه‌جا نشد.
- `CHANGELOG.md` بخش Unreleased/Changed این تغییرها را ثبت می‌کند. نام تست‌ها و مثال‌ها (`examples/cpp/outlines.cpp`،
  `examples/python/12_vector_outlines.py`) هم عوض شدند.

## کار با build و تست (همه با CMake، طبق قاعده‌ی کاربر)

```
pip install cmake ninja nanobind pytest numpy pillow fonttools --break-system-packages
cmake -S . -B /tmp/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTB_BUILD_PYTHON=OFF
cmake --build /tmp/b --target stb_test_cpp                 # تست C++ (utest)
cmake --build /tmp/b --target stb_bench_premultiply        # بنچمارک premultiply (آرگومان اول = اندازه، مثل 512)
# بدون dispatch AVX2:  -DCMAKE_CXX_FLAGS=-DSTB_NO_SIMD_DISPATCH
# پایتون:  cmake -S . -B /tmp/b3 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTB_BUILD_PYTHON=ON
#          cmake --build /tmp/b3 --target stb_test_python
# TSan:    -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" (و linker flags) سپس test_stb_cpp --filter=libstb_threads_tests.*
```

آخرین نتیجه‌ها: C++ حدود 314 تست پاس (هر دو حالت dispatch)، پایتون 1366 پاس و 4 skip (قبل از
افزودن API فونت؛ API فونت بایندینگ پایتون ندارد). TSan روی تست‌های thread بدون هشدار.
`nproc` ماشین قبلی 1 بود، پس سرعت واقعی چندهسته‌ای اندازه‌گیری نشده.

## تصمیم‌ها و قواعد کاربر

- در پایان هر کار کل پروژه zip شود (نه patch). em dash (U+2014) ممنوع (در کد، متن، مستندات). فقط با CMake build/تست شود
  (نه g++ مستقیم). قبل از پیاده‌سازی هر API جدید، API پیشنهادی توضیح داده شود و تأیید گرفته شود.
- ذخیره‌ی پیش‌فرض `stb::image` همیشه straight RGBA می‌ماند (نه permuted/premultiplied). فرمت opt-in premultiplied
  فقط اگر بنچمارک واقعی novasvg لازمش کرد.
- premultiply: امضا همان امضای خود issue است (`dst, dst_stride, src, src_stride, width, height`، stride بایتی، درجا مجاز).
  ورودی نامعتبر (`c > a`) در جهت معکوس مثل ذخیره‌ی `uint8_t` wrap می‌شود. **فرض من: مرجع novasvg هم همین است، تأیید نشده.**
- thread: پیش‌فرض auto (CPUهای مجاز: affinity و cgroup)، سقف است نه الزام؛ هر thread اضافه حدود 250us کار می‌خواهد؛
  بدون pool؛ خروجی بایت‌به‌بایت با تک‌thread یکی. ثابت‌های تخمین (1.0 ns به‌ازای هر نمونه، 0.3 برای nearest) از اندازه‌گیری
  تک‌هسته‌ی ماشین قبلی و محافظه‌کارانه‌اند؛ باید روی ماشین چندهسته‌ای تنظیم شوند. `parallel_for` فعلاً مصرف‌کننده ندارد.
- ویندوز: `<windows.h>` ماکروهای `near`، `far`، `small`، `min`، `max` دارد؛ این‌ها را اسم تابع یا متغیر نگذار (CI ویندوز با `near` در
  `test_font_outline.cpp` شکست خورد؛ شد `close_to`).
- نام‌ها: `sformat` (نه printf در نام). نسخه‌ی `{}`-style (`stb::format`) فقط اگر خواسته شود.

## کارهای باز (به ترتیب پیشنهادی)

1. **API فونت، بخش ب:** `font::shape(utf8, shape_options)` با `shaped_glyph {glyph, cluster (آفست بایت), x_advance, x_offset, y_offset}`،
   `shape_options {direction auto/ltr/rtl, kerning_mode, ligatures}`. خط لوله روی کدپوینت کار می‌کند و باید منبع هر گلیف را از
   bidi، mirror، joining عربی و ligatureهای چندبه‌یک عبور دهد. معنی ligatures=false: `liga/clig` بسته شوند، `rlig` و joining نه.
   GPOS فعلاً فقط pair adjustment دارد. `unicode-bidi: embed/override` پشتیبانی نمی‌شود. اول API را پیشنهاد بده و تأیید بگیر.
2. **WOFF/WOFF2:** libstb اصلاً ندارد (`from_memory` احتمالاً `decode_error`). WOFF1 ارزان است (zlib خودمان)، WOFF2 به Brotli نیاز دارد.
   فقط اگر novasvg خودش decode نکند.
3. بایندینگ پایتون برای API فونت (outline، glyph_index، ...)؛ فعلاً عمداً نیست.
4. kernel دستی SSE2 برای premultiply (الان auto-vectorize؛ جهت مستقیم روی alpha نیمه‌شفاف فقط ~1.3x مرجع)، NEON/arm64/MSVC/ویندوز اجرا نشده.
5. thread برای کارهای دیگر (blend با حالت‌های float، JPEG encode) فقط بعد از اندازه‌گیری روی ماشین چندهسته‌ای.
   کارهای حافظه‌محور (premultiply، flip، convert) عمداً بدون thread.
6. بایندینگ‌های تصویر پایتون موقع کار سنگین GIL را آزاد نمی‌کنند (فقط فونت‌ها می‌کنند).
7. **سیستم لاگ:** libstb ندارد (aixlog هم در ریپو و تاریخچه‌اش نیست؛ شاید در ریپوی دیگری باشد). پیشنهاد من: hook کوچک خودمان
   (`stb::set_log_handler` و `LIBSTB_LOG=off|warn|debug`، اتصال به `logging` پایتون، و دو هشدار stderr فعلی `LIBSTB_SIMD` و
   `LIBSTB_THREADS` از همان مسیر). منتظر تصمیم کاربر؛ vendor کردن aixlog با مسیر 0.9.0 (حذف وابستگی‌های vendored) جور نیست.
8. **سؤال آخر کاربر (بی‌پاسخ مانده): آیا `font` هنوز باید pimpl باشد؟** نظر من: بله. دلیل قدیمی (پنهان کردن `stb_truetype.h`)
   از بین رفته، ولی pimpl هنوز سه کار می‌کند: (الف) هدرهای داخلی (`truetype::face`، otl، arabic، `once_flag` برای GSUB lazy)
   را از هدر عمومی و نصب بیرون نگه می‌دارد؛ (ب) ABI کتابخانه‌ی shared و اشیای export شده را پایدار می‌کند؛ (ج) `shared_ptr<const impl>` همان
   معنای «کپی ارزان، immutable، thread-safe» را می‌دهد که novasvg روی آن حساب کرده (کش و قفل خودش را حذف می‌کند). هزینه‌اش یک
   اشاره‌گر اضافه در هر فراخوانی است که کنار کار هر گلیف ناچیز است.
   **کامنت‌های کهنه (اصلاح نشده، چون گفته شد کدی عوض نشود):** `include/stb/font.hpp` خطوط 3 تا 6 و 238
   (stb_truetype / stbtt_fontinfo / "no bounds checking") و `src/font/font.cpp` خط 62 هنوز از stb_truetype حرف می‌زنند؛
   باید به «موتور TrueType/CFF خود libstb (`src/font/truetype/`)» اصلاح شوند. (متن `docs/guide.md` اصلاح شده است.)
9. `checklist.md` تعداد تست‌ها و بخش «قدیمی‌ها» را کامل به‌روز نکرده‌ام (فقط آیتم‌های جدید اضافه شده).

## نکته‌های فنی که زمان تلف می‌کنند

- utest: `UTEST_PRINTF` آکولاد ندارد (داخل `if` همیشه چاپ می‌کند: `{}` بگذار)؛ `ASSERT_EQ` روی `std::string` کار نمی‌کند
  (از `==` استفاده کن)؛ عبارت assert نباید `%` داشته باشد (utest آن را فرمت printf می‌کند)؛ `<< message` ندارد.
- فونت‌های تست مصنوعی: `tests/data/make_test_font.py` (قدیمی) و `make_outline_test_fonts.py` (جدید: curves + composite + GPOS و kern،
  CFF، `.ttc`)؛ با `pip install fonttools` بازتولید می‌شوند.
- `metrics(px)` مقیاس را از (ascent - descent) می‌گیرد، نه `units_per_em`؛ برای em در SVG از `design_metrics().units_per_em` استفاده کن.
- `glyph_kerning(automatic)`: اگر فونت GPOS دارد فقط GPOS (حتی اگر جفت نداشته باشد و `kern` داشته باشد)؛ `kern_table` فقط جدول kern.
- `face_count(path)` کل فایل را می‌خواند. `glyph_info` عمداً `noexcept` نیست (CFF ممکن است تخصیص بدهد).

## issue #2: rasterizer برداری (تمام شد، منتظر review)

مسئله: https://github.com/MohammadRaziei/libstb/issues/2. همه‌ی گام‌ها انجام شد؛ جزئیات و کارهای باقی‌مانده در `checklist.md`.

### API (C++ و پایتون هم‌نام)
- `stb/path.hpp`: `matrix` (قرارداد SVG، `translate/scale/rotate/shear` به نقطه اول اعمال می‌شوند و زنجیره‌ای‌اند، `then`، `map`،
  `inverse`)، `path` (`move_to/line_to/quad_to/cubic_to/close`، `add_rect/add_rounded_rect/add_ellipse/add_circle`،
  `add_path/add_commands(…, matrix)`، `transform/transformed`، `dashed`، `bounds`، `current_point`).
- `stb/raster.hpp`: `fill(image&, path, color, fill_rule, draw_options)`, `stroke(image&, path, color, stroke_style, draw_options)`,
  و سطح پایین `rasterize_fill/rasterize_stroke(path, …, matrix, clip_rect, span_sink)` که coverage را به صورت `span_view` دسته‌ای
  گزارش می‌کنند. `draw_options` = matrix + blend + opacity. `canvas::add_path` → `path_item`.
- پایتون: `Matrix`, `Path`, `Image.fill_path/stroke_path` (کپی برمی‌گردانند)، `Path.fill_mask/stroke_mask` (تصویر gray)، `Canvas.add_path`.

### تصمیم‌ها (و تفاوت با پیشنهاد اولیه)
- الگوریتم‌ها پورت FreeType (از طریق plutovg) هستند، نه vendored. نتیجه روی ۳۱۹ صحنه (شامل ۲۴۰ صحنه‌ی fuzz) بایت‌به‌بایت با plutovg
  برابر است. حساب ثابت‌نقطه با `int64` است، پس روی همه‌ی پلتفرم‌ها یکسان؛ کل `src/draw` با `-ffp-contract=off` ساخته می‌شود.
- حذف‌شده‌ها: بخش conic (در plutovg هم ورودی conic نیست)، memory pool و band بندی (با `std::vector`؛ نتیجه یکی است).
- `path::stroked()` که در پیشنهاد اولیه بود **نیست**: stroke در فضای پیکسل با دقت 1/64 پیکسل ساخته می‌شود (مثل plutovg)، پس بازگرداندنش
  به path ی با مختصات float دقت را خراب می‌کرد. به‌جایش `rasterize_stroke` و `stroke` هست. `path::dashed` در فضای خود path کار می‌کند.
- عرض stroke با میانگین scale ماتریس ضرب می‌شود و dash قبل از ماتریس بریده می‌شود (رفتار plutovg، برای تطابق با novasvg).
- مختصات NaN/بی‌نهایت: چیزی کشیده نمی‌شود؛ بزرگ‌تر از 1e6 به 1e6 محدود می‌شود. dash نامعتبر (منفی، NaN، مجموع 0) = خط پر.
- `STB_MIN`/`STB_MAX` (در `stb/export.hpp`) جایگزین `(std::min)(...)` در کل ریپو شد (درخواست کاربر).
- هر چیز مخصوص SVG (parse path، arc، gradient، clip-path، mask) عمداً در novasvg می‌ماند.

### تست
- `tests/cpp/test_path.cpp`, `test_raster.cpp`, `test_raster_reference.cpp`; `tests/python/test_path.py`.
- مرجع plutovg: `tests/tools/raster_reference/` (`gen_scenes.py` → `scenes.h`، `reference.c` با plutovg → `reference_hashes.h`). برای
  بازتولید: دستور بالای `reference.c` را اجرا کن، خروجی را در `reference_hashes.h` بگذار. برای پیدا کردن اولین پیکسل متفاوت:
  `STB_RASTER_DUMP=dir ./test_stb_cpp` و `./reference dir2` و فایل‌های `.bin` را مقایسه کن.
- ASan/UBSan روی تست‌های raster/path/matrix تمیز بود. یک یافته‌ی UBSan از قبل در کد resize/SIMD هست (store ناهم‌تراز `double` در
  `emmintrin.h`)؛ ربطی به این تغییر ندارد و دست نزدم.
- CI ویندوز (MSVC): `near` ماکرو است (`windef.h`)، پس در تست‌ها نباید اسم `near`/`min`/`max` گذاشت؛ داخل ماکروی `EXPECT_TRUE` هم
  `point{0, 0}` با کاما ممنوع (از `pt(x, y)` استفاده شد). روی ویندوز اجرا نشده؛ CI تأیید نهایی است.

## کارهای باقی‌مانده از قبل
font part B (`font::shape`)، WOFF/WOFF2، بایندینگ پایتون برای font outline، `set_log_handler`، مرتب‌سازی `checklist.md`؛ جزئیات در همین فایل بالاتر.
