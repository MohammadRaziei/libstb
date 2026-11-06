# handoff.md (وضعیت پروژه برای ادامه‌ی کار در چت بعدی)

تاریخ: 2026-11-06 (WOFF1 و اصلاح تست numpy بعد از 2026-11-04 اضافه شدند). ریپو: mohammadraziei/libstb، شاخه‌ی `master`، آخرین commit محلی `0e84241` (`v0.9.0-8`، هشت commit بعد از 0.9.0).
این فایل untracked است (قبل از push حذفش کن). **تغییرات بخش «کارهای commit نشده» پایین هنوز commit نشده‌اند**
(working tree عمداً dirty است: حدود 90 مسیر در `git status`).

## قاعده‌های کاربر (حتماً رعایت شود)

- پاسخ‌ها فارسی. در پایان هر کار **کل پروژه zip شود** (نه patch/diff) و با `present_files` داده شود.
- **em dash (U+2014) ممنوع** (در کد، متن، مستندات، کامنت). به‌جایش دونقطه، ویرگول، پرانتز یا خط تیره‌ی ساده `-`.
- فقط با **CMake** build و تست شود، هیچ‌وقت g++ مستقیم.
- **قبل از پیاده‌سازی هر API جدید، API پیشنهادی را توضیح بده و منتظر تأیید بمان** (این بار هم برای logging همین شد).
- در API عمومی `std::shared_ptr` / `std::make_shared` نباید دیده شود (مالکیت را داخل شیء پنهان کن).
- کاربر این‌ها را **نخواست**: `stb::format` ({}-style؛ `sformat` کافی است) و کلاس `stb::file::path` (`std::filesystem::path` کافی است).
- ویندوز: `<windows.h>` ماکروهای `near`، `far`، `small`، `min`، `max` دارد؛ این‌ها را اسم تابع یا متغیر نگذار.

## کارهای commit نشده (به ترتیب پیشنهادی commit)

همه با CMake بیلد و تست شده‌اند: C++ 390 تست پاس، `ctest` کامل پاس، پایتون 1394 پاس و 4 skip (torch و uharfbuzz نصب نبود).

1. **بازنامگذاری** (فقط نام، بدون تغییر رفتار):
   - هندسه‌ی برداری `stb::path` شد `stb::outline` (`include/stb/outline.hpp`، `src/draw/outline.cpp`) با `outline_op`،
     `outline_command`، `outline_item`، `canvas::add_outline`. پایتون: `Outline`، `CanvasOutline`، `Image.fill_outline` و
     `stroke_outline`، `Canvas.add_outline`. نوع داخلی rasterizer شد `detail::fixed_outline`
     (`src/draw/raster/fixed_outline.hpp`). این نام‌ها فقط در Unreleased بودند.
   - `include/stb/shaping.hpp` شد `include/stb/rtl.hpp` (و `src/font/shaping.cpp` شد `rtl.cpp`). **یک شکست API نسبت به 0.9.0**
     (فقط نام هدر؛ `stb.h` همان را include می‌کند).
   - ساختار فایل: انکودرها و deflate رفتند `src/codec/encode/`؛ `src/font/truetype/raster.cpp` شد `glyph_raster.cpp`؛
     `bind_raster.cpp` شد `bind_outline.cpp`؛ `src/image/resize/simd.hpp` شد `simd_vec.hpp`.
   - **عمداً دست نخورد:** `stb::shape`، `draw_shape`، `shape_item`، پایتون `CanvasShape` (API منتشرشده). اگر کاربر خواست عوض شود،
     پیشنهاد قبلی `draw_primitive` بود. `include/stb/export.hpp` (دارای `STB_MIN/STB_MAX`) هم جابه‌جا نشد.
2. **اصلاح CI ویندوز:** در `tests/cpp/test_font_outline.cpp` تابع `near` شد `close_to`؛ در `examples/cpp/tour.cpp` متغیر `small`
   شد `tiny`. **روی CI ویندوز هنوز اجرا نشده**؛ بعد از push لاگ را بررسی کن (در لاگ قبلی فقط همان یک خطای C2062 بود؛
   بقیه warning بودند: C4244 در `bidi.cpp`، C4996 برای `fopen`/`getenv`).
3. **`stb::logging`** (جدید، کامل در C++): `include/stb/logging.hpp` + `src/util/logging.cpp` + `tests/cpp/test_logging.cpp` (19 تست).
   - مدل Python `logging`: `get_logger("libstb.simd")` با سلسله‌ی نقطه‌ای، `logger` (سطح مؤثر از جدّ، `propagate`، printf-style
     `debug/info/warning/error/critical/log/vlog` روی `sformat`)، `handler` (`stream_handler`، `file_handler`، `callback_handler`،
     `null_handler` یا کلاس دلخواه با override کردن `emit`)، `formatter` (الگوی Python: `%(asctime)s [%(levelname)-8s] %(name)s: %(message)s`)،
     `basic_config`. سطح‌ها همان عددهای Python (10 تا 50) و `notset`.
   - `logger::add_handler(H)` handler را **by value** می‌گیرد و `H&` برمی‌گرداند (shared_ptr فقط داخل است)؛ `remove_handler(h)`.
   - registry داخل `.cpp` است (نه header، چون کتابخانه shared است) و هرگز نابود نمی‌شود. logging هیچ‌وقت exception نمی‌دهد.
   - «last resort» مثل Python: اگر هیچ handler در سلسله نباشد، warning به بالا روی stderr می‌رود به‌شکل `<logger>: <message>`.
   - `LIBSTB_LOG=debug|info|warning|error|critical|<عدد>` سطح logger `libstb` را تنظیم می‌کند.
   - دو هشدار قبلی stderr (`LIBSTB_SIMD`، `LIBSTB_THREADS`) حالا از `libstb.simd` و `libstb.threads` رد می‌شوند. **تغییر خروجی:** پیشوند
     قبلی `libstb: ` شد `libstb.threads: ` / `libstb.simd: `.
   - از aixlog (https://github.com/badaix/aixlog، v1.5.0، MIT) فقط **ایده** گرفته شد (مفهوم sink، sinkهای پلتفرمی)؛ **هیچ کدی کپی نشده**،
     پس `NOTICE.md` دست نخورده. aixlog را در `/tmp/aixlog` clone کرده بودم (موقت است؛ دوباره clone کن).
   - مستندات: `docs/guide.md` (بخش Logging)، `CHANGELOG.md` (Added و Changed در Unreleased)، `checklist.md`.

4. **پل پایتون برای logging** (تمام شد، commit نشده): `src/bindings/python/bind_logging.cpp` + `tests/python/test_logging.py` (15 تست).
   با `import libstb` خودکار نصب می‌شود (تأیید کاربر)؛ یک `callback_handler` روی `libstb` که رکورد را با GIL به
   `logging.getLogger(name).log(levelno, message)` می‌دهد. سطح C++ روی `debug` باز می‌شود مگر `LIBSTB_LOG` تنظیم باشد. تنها تابع عمومی:
   `libstb.logging_bridge(enabled)`؛ `libstb_py._log(name, level, msg)` خصوصی و فقط برای تست است. `Py_IsFinalizing` در stable ABI نیست
   (فقط `Py_IsInitialized`). **نکته:** اگر روزی thread کارگر libstb log کند، callback روی GIL منتظر می‌ماند و ممکن است با thread صدازننده‌ای که
   GIL را نگه داشته و join می‌کند deadlock شود؛ الان فقط دو هشدار env روی thread صدازننده log می‌شوند.

5. **logging خود کتابخانه + ADRها** (تمام شد، commit نشد): `src/util/log.hpp` (loggerهای `libstb.decode|encode|resize|zlib|font|raster|simd|threads`)
   و فراخوانی‌ها در `image.cpp` (decode/encode)، `resize.cpp`، `simd.cpp`، `threads.cpp`، `zlib.cpp`، `font.cpp` (glyph گمشده)،
   `draw/raster.cpp` و `draw/raster/dash.cpp` (هشدارها). سیاست در `docs/adr/0003-logging-policy.md`: DEBUG یعنی «چه تصمیمی گرفتم»،
   WARNING فقط وقتی از درخواست منحرف شدم، بدون INFO/ERROR، هیچ log از thread کارگر `parallel.hpp` (خطر deadlock روی GIL)،
   خطا فقط exception است. هزینه‌ی پل: حدود 0.6 میکروثانیه به‌ازای هر فراخوانی log‌شده (پل loggerهای پایتون را cache می‌کند و قبل از ساختن رشته‌ها `isEnabledFor` می‌پرسد؛ قبلاً 2.5 تا 5 بود؛ اندازه‌گیری در ADR-0002؛ سطح C++ عمداً همیشه DEBUG می‌ماند چون پایتون hook برای setLevel ندارد). تست‌ها:
   `tests/cpp/test_library_logging.cpp` (6) و سه تست در `tests/python/test_logging.py`. ADRها: `docs/adr/0001..0003`
   (0001 بعد از کد و از روی طراحی موجود نوشته شد). نکته: `to_cubic_commands` مختصات NaN را پیش از `make_outline` دور می‌ریزد، پس
   هشدار مختصات ورودی آنجاست و هشدار `make_outline` فقط برای ماتریس NaN است. باگ MSVC در `test_logging.cpp` (`each` در lambda) هم اصلاح شد.

6. **CI ویندوز و warningها** (تمام شد، commit نشد): تست `test_file_handler...` روی ویندوز fail بود چون `file_handler` در حالت text می‌نویسد (CRLF، مثل `FileHandler` پایتون) ولی `slurp` باینری می‌خواند؛ `slurp` حالا text می‌خواند. warningها از ریشه درست شدند، نه خاموش: `stroke_style::dashes{}`، `int(cl.x - x)` در gray_raster، تبدیل صریح `level_` در bidi، `if constexpr ... else` به‌جای کد بعد از `return` در `composite_row`، تغییر نام `code` محلی در jpeg، و در تست‌ها `ofstream`/`stb::env`/`tmpfile_s` به‌جای fopen/getenv/tmpfile. تست‌هایی که عمداً ورودی بد می‌دهند حالا هشدار libstb را با `testutil::log_capture` می‌گیرند و می‌شمارند (خروجی تست ساکت است). بیلد تمیز gcc: صفر warning. ویندوز را نتوانستم اجرا کنم.

7. **WOFF1** (تمام شد، commit نشد): `src/font/woff.hpp` و `woff.cpp` (`detail::woff1_to_sfnt`، `web_font_signature`)، اتصال در
   `src/font/font.cpp` (`unpack_web_font` در `from_memory` و `open`؛ `font_bytes::own` اگر mapping داشته باشد اول `munmap` می‌کند؛
   `face_count` برای `wOFF` ۱ می‌دهد و برای `wOF2` پیام «WOFF2 fonts are not supported»). فقط تشخیص خودکار، بدون تابع عمومی جدید
   (API را کاربر تأیید کرد). تصمیم‌ها: `totalSfntSize` و بلوک‌های metadata/private استفاده نمی‌شوند (اندازه‌ی sfnt از خود جدول‌ها
   حساب می‌شود)؛ `length` باید برابر اندازه‌ی فایل باشد؛ `reserved` چک نمی‌شود؛ ترتیب داده‌ی جدول‌ها در فایل مهم نیست ولی هم‌پوشانی و
   تگ ناصعودی رد می‌شود؛ سقف ۵۱۲ MiB (`limit_error`) و حافظه فقط به اندازه‌ی داده‌ی واقعاً تولیدشده رزرو می‌شود. تست:
   `tests/cpp/test_woff.cpp` (۱۵ تست) و ۲ تست در `tests/python/test_font.py`؛ فونت‌های `tests/data/*.woff` را
   `tests/data/make_woff_test_fonts.py` می‌سازد (از fontTools). تغییر عمومی: کامنت‌های `include/stb/font.hpp`، `docs/guide.md`، `CHANGELOG.md`.
8. **اصلاح CI ویندوز ARM (wheel):** سه تست `tests/python/test_logging.py` مستقیم `import numpy` می‌کردند و روی runner بدون numpy fail
   شدند؛ حالا `pytest.importorskip("numpy")` داخل همان سه تست است (نه سطح ماژول، تا بقیه‌ی تست‌های logging اجرا شوند).

پیشنهاد commit: پنج commit جدا (بازنامگذاری، اصلاح ویندوز، logging در C++، پل پایتون، logging کتابخانه و ADRها)، بعد commitهای کوچک
(WOFF1، اصلاح تست numpy، `font::shape`، handlerهای OS log).

## کارهای باز (به ترتیب پیشنهادی)

1. **اول CI را بررسی کن:** دو کار بعدی (`font::shape` و handlerهای OS log) **بدون هیچ بیلد و اجرا** نوشته شدند (به درخواست کاربر؛ او خطاها
   را از CI می‌آورد). هر خطای کامپایل یا تست قرمز از اینهاست تا ثابت شود نه. احتمال بیشتر: متن تست‌های `tests/cpp/test_font_shape.cpp`
   (انتظارهای clusterهای فارسی با فونت `libstb-fa-gsub-test.ttf` از روی استدلال نوشته شد، نه اجرا)، و `src/util/logging_os.cpp` روی macOS
   (os_log) و ویندوز (Event Log). Android هیچ CI ندارد و فقط نوشته شده.
2. **`font::shape` (انجام شد، اجرا نشده):** `include/stb/font.hpp` (`text_direction`، `shape_options`، `shaped_glyph`، `font::shape`)،
   پیاده‌سازی در `src/font/font.cpp`. خط لوله: `glyph_record::cluster` (`gsub.hpp`) تا آخر می‌رود؛ نوع داخلی `shaped_glyph` شد `visual_glyph`
   (نام عمومی آزاد شود) و حالا `cluster` دارد؛ `text_shaper::shape(text, shaper_options{direction, ligatures})`؛ `bidi::resolve(classes,
   base_dir)`؛ `arabic_layout::plan_plain` همان plan بدون `liga/clig` است. تصمیم‌ها: مکان marks از ink boxها در واحد فونت (همان قاعده‌ی
   `do_layout` بدون گرد کردن، پس تا حدود یک پیکسل با `draw_text` فرق دارد و عمداً `do_layout` به `shape` تبدیل نشد تا pixel goldenها نشکنند)؛
   kerning به `x_advance` گلیف اول جفت اضافه می‌شود؛ markها جفت را نمی‌شکنند؛ `'\n'` گلیف 0 با cluster خودش؛ `'\r'` حذف؛ هر خط جدا.
   **محدودیت شناخته‌شده:** GSUB فقط برای متن راست‌به‌چپ اجرا می‌شود (مثل `draw_text`)، پس `ligatures` روی متن لاتین اثری ندارد (liga لاتین
   هنوز اجرا نمی‌شود)؛ mark positioning از GPOS نیست؛ `unicode-bidi: embed/override` پشتیبانی نمی‌شود. بایندینگ پایتون عمداً ندارد.
   تست: `tests/cpp/test_font_shape.cpp`.
2b. **handlerهای OS log (انجام شد، اجرا نشده):** `include/stb/logging_os.hpp` (`syslog_handler`، `android_handler`، `unified_log_handler`،
   `debug_string_handler`، `event_log_handler`، ماکروهای `STB_LOG_HAS_*`)، `src/util/logging_os.cpp`، `src/util/logging_os_rank.hpp` (نگاشت سطح به
   ۵ severity، قابل تست روی همه‌ی پلتفرم‌ها)، لینک `advapi32` (ویندوز) و `log` (اندروید) در `libstb_apply_core_build_settings`. **کد از aixlog
   کپی نشده** (از API خود سیستم‌عامل‌ها نوشته شد)، پس در `NOTICE.md` چیزی ثبت نشد؛ اگر خواستید نام الهام را ذکر کنید بگویید. ثبت مکان و `exception()`
   اضافه نشد (فقط اگر کاربر خواست). تست: `tests/cpp/test_logging_os.cpp`. نکته: syslog یک اتصال برای کل پروسه دارد (`openlog`)؛ در Event Viewer
   بدون message file متن بعد از «description cannot be found» می‌آید.
3. **WOFF2:** نیاز به Brotli (decoder حدود چند صد KB داده‌ی دیکشنری)؛ فقط اگر novasvg خودش decode نکند. (WOFF1 تمام شد، بالا ببین.)
4. **بایندینگ پایتون برای API فونت** (outline، glyph_index و ...) و برای `Outline(font.outline(g))`؛ فعلاً عمداً نیست.
6. kernel دستی SSE2 برای premultiply (الان auto-vectorize؛ جهت مستقیم روی alpha نیمه‌شفاف فقط حدود 1.3x مرجع)؛ NEON/arm64/MSVC/ویندوز اجرا نشده.
7. thread برای کارهای دیگر (blend با حالت‌های float، JPEG encode) فقط بعد از اندازه‌گیری روی ماشین چندهسته‌ای. کارهای حافظه‌محور
   (premultiply، flip، convert) عمداً بدون thread. `nproc` ماشین قبلی 1 بود.
8. بایندینگ‌های تصویر پایتون موقع کار سنگین GIL را آزاد نمی‌کنند (فقط فونت‌ها می‌کنند).
9. بنچمارک rasterizer (fill/stroke نسبت به plutovg و Pillow/cairo)؛ تأیید novasvg روی goldenهای خودش (باید با تلرانس 0 برابر باشد؛ اگر
   نبود اول تفاوت FMA/پرچم کامپایل را بررسی کن).
10. **سؤال بی‌پاسخ: آیا `font` هنوز باید pimpl باشد؟** نظر من: بله (هدرهای داخلی را از هدر عمومی بیرون نگه می‌دارد؛ ABI پایدار؛
    `shared_ptr<const impl>` یعنی «کپی ارزان، immutable، thread-safe»، که novasvg روی آن حساب کرده). **کامنت‌های کهنه (اصلاح نشده):**
    `include/stb/font.hpp` خطوط 3 تا 6 و 238 (stb_truetype / stbtt_fontinfo) و `src/font/font.cpp` خط 62 باید به «موتور TrueType/CFF خود libstb
    (`src/font/truetype/`)» اصلاح شوند.
11. `checklist.md`: تعداد تست‌ها و بخش «قدیمی‌ها» کامل به‌روز نشده (فقط آیتم‌های جدید اضافه شده‌اند).

## کار با build و تست (همه با CMake)

```
pip install cmake ninja nanobind pytest numpy pillow fonttools --break-system-packages
cmake -S . -B /tmp/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTB_BUILD_PYTHON=ON -DSTB_BUILD_EXAMPLES=ON
cmake --build /tmp/b -- -j2                       # همه چیز (یک هسته: چند دقیقه؛ آن را detach کن: setsid nohup ... &)
cmake --build /tmp/b --target test_stb_cpp        # فقط تست C++ (utest)
cd /tmp/b && ctest                                # کل ctest
PYTHONPATH=/tmp/b/python/install python3 -m pytest tests/python -q     # تست پایتون روی ماژول بیلدشده
./tests/cpp/test_stb_cpp --filter='libstb_logging_tests.*'              # فیلتر utest
# بنچمارک premultiply:  cmake --build /tmp/b --target stb_bench_premultiply
# بدون dispatch AVX2:   -DCMAKE_CXX_FLAGS=-DSTB_NO_SIMD_DISPATCH
# TSan: -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" (و linker flags) سپس test_stb_cpp --filter=libstb_threads_tests.*
```

نکته: `pgrep -f "cmake --build"` در یک دستور bash خودش را هم پیدا می‌کند؛ برای دیدن اجرای build از `pgrep -x ninja` استفاده کن.

## تصمیم‌ها از چت‌های قبل

- ذخیره‌ی پیش‌فرض `stb::image` همیشه straight RGBA می‌ماند (نه permuted/premultiplied). فرمت opt-in premultiplied فقط اگر بنچمارک واقعی novasvg لازمش کرد.
- premultiply: امضا همان امضای issue #1 (`dst, dst_stride, src, src_stride, width, height`، stride بایتی، درجا مجاز). ورودی نامعتبر
  (`c > a`) در جهت معکوس مثل ذخیره‌ی `uint8_t` wrap می‌شود. **فرض من: مرجع novasvg هم همین است، تأیید نشده.**
- thread: پیش‌فرض auto (CPUهای مجاز: affinity و cgroup)، سقف است نه الزام؛ هر thread اضافه حدود 250us کار می‌خواهد؛ بدون pool؛ خروجی
  بایت‌به‌بایت با تک‌thread یکی. ثابت‌های تخمین (1.0 ns به‌ازای هر نمونه، 0.3 برای nearest) از تک‌هسته‌ی ماشین قبلی‌اند و باید روی چندهسته‌ای تنظیم شوند.
  `parallel_for` فعلاً مصرف‌کننده ندارد.
- نام‌ها: `sformat` (نه printf در نام). `STB_MIN/STB_MAX` (در `stb/export.hpp`) جای `(std::min)(...)` در کل ریپو.
- commitهای قبلی: `0f3c5d0` issue #1 (`rgba_to_premultiplied_argb` / `premultiplied_argb_to_rgba`، SIMD)، `685f126` threading
  (`thread_count/set_threads/thread_scope`، `LIBSTB_THREADS`، `src/util/parallel.hpp`)، `8e76e18` `sformat/vsformat/sformat_to`،
  `657ae77` API گلیف-ایندکس `stb::font` (بخش الف novasvg)، `8845b4d` و `0e84241` issue #2 (rasterizer برداری).

## نکته‌های فنی که زمان تلف می‌کنند

- utest: `UTEST_PRINTF` آکولاد ندارد (داخل `if` همیشه چاپ می‌کند: `{}` بگذار)؛ `ASSERT_EQ` روی `std::string` کار نمی‌کند (از `==` یا یک ماکروی
  `ASSERT_SAME` محلی استفاده کن)؛ عبارت assert نباید `%` داشته باشد؛ `<< message` ندارد. داخل ماکروی assert کاما در `point{0, 0}` ممنوع است (از `pt(x, y)`).
- فونت‌های تست مصنوعی: `tests/data/make_test_font.py` و `make_outline_test_fonts.py` (curves، composite، GPOS و kern، CFF، `.ttc`)؛ با `pip install fonttools` بازتولید می‌شوند.
- `metrics(px)` مقیاس را از (ascent - descent) می‌گیرد، نه `units_per_em`؛ برای em در SVG از `design_metrics().units_per_em` استفاده کن.
- `glyph_kerning(automatic)`: اگر فونت GPOS دارد فقط GPOS؛ `kern_table` فقط جدول kern. `face_count(path)` کل فایل را می‌خواند. `glyph_info` عمداً `noexcept` نیست.
- هدر `export.hpp` در MSVC هشدارهای C4251/C4275 را خاموش می‌کند؛ کلاس‌های export شده می‌توانند عضو `std::` داشته باشند.
- برای printf-check روی متد عضو، اندیس‌ها یکی بیشتر است (`this` آرگومان 1): `STB_FORMAT_CHECK(2, 3)`.

## rasterizer برداری (issue #2): تمام شد، commit شده

مسئله: https://github.com/MohammadRaziei/libstb/issues/2. کارهای باقی‌مانده بالا (بنچمارک، تأیید novasvg) هستند.

### API (C++ و پایتون هم‌نام)
- `stb/outline.hpp`: `matrix` (قرارداد SVG، `translate/scale/rotate/shear` به نقطه‌ی اول اعمال می‌شوند و زنجیره‌ای‌اند، `then`، `map`، `inverse`)،
  `outline` (`move_to/line_to/quad_to/cubic_to/close`، `add_rect/add_rounded_rect/add_ellipse/add_circle`، `add_outline/add_commands(..., matrix)`،
  `transform/transformed`، `dashed`، `bounds`، `current_point`).
- `stb/raster.hpp`: `fill(image&, outline, color, fill_rule, draw_options)`، `stroke(image&, outline, color, stroke_style, draw_options)` و سطح پایین
  `rasterize_fill/rasterize_stroke(outline, ..., matrix, clip_rect, span_sink)` که coverage را به‌صورت `span_view` دسته‌ای گزارش می‌کنند.
  `draw_options` = matrix + blend + opacity. `canvas::add_outline` → `outline_item`.
- پایتون: `Matrix`، `Outline`، `Image.fill_outline/stroke_outline` (کپی برمی‌گردانند)، `Outline.fill_mask/stroke_mask` (تصویر gray)، `Canvas.add_outline`.

### تصمیم‌ها
- الگوریتم‌ها پورت FreeType (از طریق plutovg) هستند، نه vendored. روی 319 صحنه (شامل 240 صحنه‌ی fuzz) بایت‌به‌بایت با plutovg برابر است.
  حساب ثابت‌نقطه با `int64` است (روی همه‌ی پلتفرم‌ها یکسان)؛ کل `src/draw` با `-ffp-contract=off` ساخته می‌شود.
- حذف‌شده‌ها: بخش conic، memory pool و band بندی (با `std::vector`؛ نتیجه یکی است).
- `outline::stroked()` عمداً **نیست**: stroke در فضای پیکسل با دقت 1/64 پیکسل ساخته می‌شود (مثل plutovg). به‌جایش `rasterize_stroke` و `stroke` هست.
  `outline::dashed` در فضای خود outline کار می‌کند.
- عرض stroke با میانگین scale ماتریس ضرب می‌شود و dash قبل از ماتریس بریده می‌شود (رفتار plutovg).
- مختصات NaN/بی‌نهایت: چیزی کشیده نمی‌شود؛ بزرگ‌تر از 1e6 به 1e6 محدود می‌شود. dash نامعتبر (منفی، NaN، مجموع 0) = خط پر.
- هر چیز مخصوص SVG (parse path، arc، gradient، clip-path، mask) عمداً در novasvg می‌ماند.

### تست
- `tests/cpp/test_outline.cpp`، `test_raster.cpp`، `test_raster_reference.cpp`؛ `tests/python/test_outline.py`.
- مرجع plutovg: `tests/tools/raster_reference/` (`gen_scenes.py` → `scenes.h`، `reference.c` با plutovg → `reference_hashes.h`). برای بازتولید:
  دستور بالای `reference.c` را اجرا کن و خروجی را در `reference_hashes.h` بگذار. برای پیدا کردن اولین پیکسل متفاوت:
  `STB_RASTER_DUMP=dir ./test_stb_cpp` و `./reference dir2` و فایل‌های `.bin` را مقایسه کن.
- ASan/UBSan روی تست‌های raster/outline/matrix تمیز بود. یک یافته‌ی UBSan از قبل در کد resize/SIMD هست (store ناهم‌تراز `double` در `emmintrin.h`)؛ ربطی به این تغییرها ندارد.
