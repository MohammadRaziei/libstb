# handoff.md (وضعیت پروژه برای ادامه‌ی کار در چت بعدی)

تاریخ: 2026-11-09 (بعد از 2026-11-06: GPOS برای kerning و mark، `glyph_kind`، `shape` با بازه، ترتیب marks؛ بخش 9 پایین). ریپو: mohammadraziei/libstb، شاخه‌ی `master`، آخرین commit محلی `0e84241` (`v0.9.0-8`، هشت commit بعد از 0.9.0).
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

**به‌روزرسانی (آخرین دور):** کارهای شماره‌دار پایین (بازنامگذاری، logging، shape، WOFF1، API فونت پایتون، `to_pil`) بین‌راه در تاریخچه‌ی git commit شده‌اند؛
فقط «این دور» (بخش بعدی: `data/`، مثال‌ها، تست‌ها، اصلاح leak) commit نشده است.

همه با CMake بیلد و تست شده‌اند (gcc، لینوکس، `-j2`): C++ 442 تست پاس (از جمله `font::shape`، `logging_os` و 17 تست `test_sample_data.cpp`)، `ctest` کامل پاس، صفر warning،
پایتون 1624 پاس و 4 skip (torch نصب نبود؛ uharfbuzz نصب بود و تست‌های مقایسه با HarfBuzz اجرا شدند). ویندوز، macOS و Android هنوز اجرا نشده‌اند.

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

9. **GPOS، `glyph_kind`، `shape(text, begin, end)`** (تمام شد، commit نشده؛ سه issue که کاربر پیشنهاد داد + دو issue بعدی). همه با CMake بیلد و تست شد
   (gcc، لینوکس): C++ 453 تست پاس، `ctest` سه از سه، صفر warning. ویندوز/macOS/MSVC اجرا نشده.
   - **موتور GPOS** (`src/font/shaper/gpos.hpp`، `gpos.cpp`؛ کلاس `otl::positioner`، جای `mark_positioner` که در همین دور ساخته و عوض شد). بعد از GSUB و
     روی glyphها به **ترتیب منطقی** (مثل HarfBuzz) اجرا می‌شود. featureها: `kern`، `dist`، `mark`، `mkmk`، `abvm`، `blwm` برای اولین اسکریپت
     موجود از `arab` و `DFLT` (زبان پیش‌فرض)، lookupها به ترتیب شماره‌ی lookup. نوع‌ها: 1 (single)، 2 (pair، قالب 1 و 2)، 4، 5، 6 (mark به base،
     ligature، mark)، 7 و 8 (context و chaining، هر سه قالب)، 9 (extension). **نوع 3 (cursive) و device/variation tables خوانده نمی‌شوند.**
     گزینش lookup بر اساس feature است؛ قبلاً `face.cpp` همه‌ی lookupهای نوع 2 را اسکن می‌کرد (برای لاتین هنوز همین است، بخش «باز» پایین).
     امنیت: همه‌ی خواندن‌ها range-check، سقف lookup (1024)، subtable (16384)، عمق تو در تو (8)، گلیف در rule (255) و بودجه‌ی تلاش
     (4096 به‌ازای هر glyph، حداکثر 2^26).
   - خروجی در `glyph_record` / `visual_glyph`: `attached`، `mark_x`، `mark_y` (مختصات mark از origin پایه، واحد فونت، y رو به بالا؛ زنجیره‌ی mark-to-mark جمع می‌شود)،
     `kern` (به advance همان glyph اضافه می‌شود)، `shift_x`، `shift_y` (جابه‌جایی رسم)، و در `visual_glyph` فلگ `kerned`: glyph عربی در خطی که فونت
     برایش lookup `kern`/`dist` دارد؛ جفت دو glyph `kerned` دوباره با `face.glyph_kerning` kern نمی‌شود (جلوگیری از اعمال دوباره).
     `arabic_layout::gpos` (`marks` قبلی) مستقل از GSUB ساخته می‌شود (فونت بدون GSUB ولی با GPOS هم کار می‌کند).
   - `font::shape` و `do_layout` (برای `draw_text`) **از یک خروجی** استفاده می‌کنند، پس marks در هر دو یکی است (فقط گرد شدن پیکسل در `draw_text`).
     mark بدون anchor همان قاعده‌ی قدیمی (وسط‌چین روی ink) را می‌گیرد. `kerning=none` فقط `kern` را حذف می‌کند، نه mark را.
   - **ترتیب marks:** `shadda_first` شد `order_marks` (مرتب‌سازی پایدار با combining class، شدّه اول؛ `mark_order` در `text_shaper.cpp`)، هم قبل از GPOS هم
     بعد از bidi. **mark بدون حرف** (بعد از فاصله یا اول متن) حالا advance صفر می‌گیرد (مثل HarfBuzz).
   - **`glyph_kind`** (`normal`، `notdef`، `line_break`) در `shaped_glyph::kind`؛ پایتون: عنصر ششم تاپل C++ ← `ShapedGlyph.kind` (رشته، پیش‌فرض `"normal"`).
   - **`font::shape(utf8, begin, end, options)`:** کل متن شکل می‌گیرد و فقط glyphهایی که cluster‌شان در `[begin, end)` است برمی‌گردند؛ `cluster` همچنان
     آفست در کل متن؛ لبه‌ی وسط UTF-8 یا بازه‌ی بد `invalid_argument`. هزینه: هر فراخوانی کل متن را شکل می‌دهد. پایتون: `Font.shape(..., begin=0, end=None)` (بایت).
   - **امضای API این دور:** `kind` (گزینه‌ی الف: enum) و بازه (امضای `begin, end`) را خودم انتخاب کردم چون کاربر گفت «به صلاح‌دید خودت»؛ کاربر رد نکرد.
   - **تست‌ها:** `tests/cpp/test_font_shape.cpp` (11 تست جدید: `kind`، GPOS mark به base/ligature/mark، mark بدون anchor، بازه و جوینینگ، افراز دلخواه متن)،
     فونت مصنوعی `tests/data/libstb-fa-gpos-test.ttf` (+ `make_mark_test_font.py`؛ مقدارهای دست‌حساب‌شده با HarfBuzz هم یکی بود)،
     `tests/python/test_sample_fonts.py::test_vowelled_persian_text_is_positioned_like_harfbuzz_...` (Vazirmatn Regular، 300 متن، kern روشن، ترتیب دلخواه marks، صفر اختلاف).
     تست قدیمی HarfBuzz همان فایل حالا `kerning="none"` می‌دهد (چون kerning عربی حالا کار می‌کند و آن تست kern را خاموش می‌کند).
   - **مقایسه با HarfBuzz روی فونت‌های واقعی** (فقط محلی، فونت‌ها در repo نیستند؛ از releaseهای https://github.com/MohammadRaziei/fonts: `fa-Vazirmatn`، `fa-MjSeries`،
     `fa-pro-archive`، با `curl -L .../releases/download/<tag>/fonts.zip`؛ API گیت‌هاب rate-limit بود): 300 متن تصادفی حرکت‌دار، kern روشن:
     Vazirmatn (Regular، Bold، Thin)، Scheherazade و Lateef **300 از 300 دقیق**؛ IRANSans 279؛ Amiri حدود 171 از 268؛ Noor_e_Quran 234 از 291.
     `tools/compare_with_harfbuzz.py` همین مقایسه را اجرا می‌کند.
   - docs: `CHANGELOG.md`، `docs/guide.md`، `README.md`، کامنت‌های `include/stb/font.hpp`.

پیشنهاد commit: پنج commit جدا (بازنامگذاری، اصلاح ویندوز، logging در C++، پل پایتون، logging کتابخانه و ADRها)، بعد commitهای کوچک
(WOFF1، اصلاح تست numpy، `font::shape`، handlerهای OS log، و برای بخش 9: «GPOS positioner» (gpos.*، otl، text_shaper، font.cpp، تست‌ها)،
«glyph_kind» و «shape با بازه» جدا از آن).

## کارهای باز (به ترتیب پیشنهادی)

1. **CI را روی ویندوز، macOS و Android بررسی کن:** `font::shape` و handlerهای OS log حالا روی لینوکس بیلد و اجرا شدند (11 تست shape
   و تست‌های `logging_os` پاس؛ انتظارهای clusterهای فارسی درست بودند)، ولی `src/util/logging_os.cpp` روی macOS (os_log) و ویندوز
   (Event Log) و Android (logcat) هنوز اجرا نشده و ویندوز/ARM/MSVC هم برای اصلاحات این دوره. هر خطای CI احتمالاً از اینهاست.
2. **`font::shape` (انجام شد، روی لینوکس اجرا و پاس):** `include/stb/font.hpp` (`text_direction`، `shape_options`، `shaped_glyph`، `font::shape`)،
   پیاده‌سازی در `src/font/font.cpp`. خط لوله: `glyph_record::cluster` (`gsub.hpp`) تا آخر می‌رود؛ نوع داخلی `shaped_glyph` شد `visual_glyph`
   (نام عمومی آزاد شود) و حالا `cluster` دارد؛ `text_shaper::shape(text, shaper_options{direction, ligatures})`؛ `bidi::resolve(classes,
   base_dir)`؛ `arabic_layout::plan_plain` همان plan بدون `liga/clig` است. تصمیم‌ها: مکان marks: از GPOS (بخش 9) و برای mark بی‌anchor
   از ink boxها در واحد فونت؛ `do_layout` به `shape` تبدیل نشد تا pixel goldenها نشکنند ولی هر دو از همان خروجی `visual_glyph` می‌خوانند؛
   kerning به `x_advance` گلیف اول جفت اضافه می‌شود؛ markها جفت را نمی‌شکنند؛ `'\n'` گلیف 0 با cluster خودش؛ `'\r'` حذف؛ هر خط جدا.
   **محدودیت شناخته‌شده:** GSUB فقط برای متن راست‌به‌چپ اجرا می‌شود (مثل `draw_text`)، پس `ligatures` روی متن لاتین اثری ندارد (liga لاتین
   هنوز اجرا نمی‌شود)؛ `unicode-bidi: embed/override` پشتیبانی نمی‌شود. بایندینگ پایتون: `Font.shape` (بخش «پایتون: API فونت» پایین).
   تست: `tests/cpp/test_font_shape.cpp` (اجرا شد، پاس).
2b. **handlerهای OS log (انجام شد؛ فقط لینوکس/syslog اجرا شد):** `include/stb/logging_os.hpp` (`syslog_handler`، `android_handler`، `unified_log_handler`،
   `debug_string_handler`، `event_log_handler`، ماکروهای `STB_LOG_HAS_*`)، `src/util/logging_os.cpp`، `src/util/logging_os_rank.hpp` (نگاشت سطح به
   ۵ severity، قابل تست روی همه‌ی پلتفرم‌ها)، لینک `advapi32` (ویندوز) و `log` (اندروید) در `libstb_apply_core_build_settings`. **کد از aixlog
   کپی نشده** (از API خود سیستم‌عامل‌ها نوشته شد)، پس در `NOTICE.md` چیزی ثبت نشد؛ اگر خواستید نام الهام را ذکر کنید بگویید. ثبت مکان و `exception()`
   اضافه نشد (فقط اگر کاربر خواست). تست: `tests/cpp/test_logging_os.cpp`. نکته: syslog یک اتصال برای کل پروسه دارد (`openlog`)؛ در Event Viewer
   بدون message file متن بعد از «description cannot be found» می‌آید.
3. **WOFF2:** نیاز به Brotli (decoder حدود چند صد KB داده‌ی دیکشنری)؛ فقط اگر novasvg خودش decode نکند. (WOFF1 تمام شد، بالا ببین.)
4. **بایندینگ پایتون برای API فونت: تمام شد (commit نشده).** بخش «پایتون: API فونت» پایین را ببین. handlerهای OS log و کلاس‌های
   `get_logger/handler/basic_config` عمداً بایند نشدند (پایتون `logging` و handlerهای خودش را دارد و پل همه‌ی logها را به آن می‌فرستد).
6. kernel دستی SSE2 برای premultiply (الان auto-vectorize؛ جهت مستقیم روی alpha نیمه‌شفاف فقط حدود 1.3x مرجع)؛ NEON/arm64/MSVC/ویندوز اجرا نشده.
7. thread برای کارهای دیگر (blend با حالت‌های float، JPEG encode) فقط بعد از اندازه‌گیری روی ماشین چندهسته‌ای. کارهای حافظه‌محور
   (premultiply، flip، convert) عمداً بدون thread. `nproc` ماشین قبلی 1 بود.
8. بایندینگ‌های تصویر پایتون موقع کار سنگین GIL را آزاد نمی‌کنند (فونت‌ها می‌کنند). آزاد کردن هزینه دارد (چند ده تا چند صد ns و ممکن است
   تا یک switch interval برای گرفتن دوباره صبر کنی)، پس فقط وقتی کار حدود 50 تا 100 میکروثانیه یا بیشتر است. آستانه‌ی بهینه برای تصویر را
   باید اندازه‌گیری کرد (مثلاً بر اساس تعداد پیکسل)؛ تغییر API نیست.
9. بنچمارک rasterizer (fill/stroke نسبت به plutovg و Pillow/cairo)؛ تأیید novasvg روی goldenهای خودش (باید با تلرانس 0 برابر باشد؛ اگر
   نبود اول تفاوت FMA/پرچم کامپایل را بررسی کن).
10. **سؤال بی‌پاسخ: آیا `font` هنوز باید pimpl باشد؟** نظر من: بله (هدرهای داخلی را از هدر عمومی بیرون نگه می‌دارد؛ ABI پایدار؛
    `shared_ptr<const impl>` یعنی «کپی ارزان، immutable، thread-safe»، که novasvg روی آن حساب کرده). **کامنت‌های کهنه اصلاح شد:** `include/stb/font.hpp` (بالای فایل و pimpl)، `src/font/font.cpp` و docstring بالای `font.py`
    دیگر نمی‌گویند موتور stb_truetype است؛ حالا «موتور خود libstb» و «reader مرزها را چک می‌کند ولی fuzz نشده» (همان جمله‌ی `docs/guide.md`).
    تناقض guide/هدر درباره‌ی GPOS حل شد: کد (`face.cpp`) و تست (`test_kerning_modes_pick_gpos_or_the_kern_table`) نشان می‌دهند GPOS pair
    adjustment (lookup نوع ۲، فقط advance گلیف اول) خوانده می‌شود، پس `docs/guide.md` و `checklist.md` اصلاح شدند. (بخش 9 بالا: بعداً GPOS
    کامل‌تر شد؛ فقط cursive و device tables نیستند). ورودی تاریخی `CHANGELOG.md` (نسخه‌ی قدیمی) عمداً دست نخورد.
12. **GPOS، کارهای باقی‌مانده (از بخش 9):**
    - **cursive attachment (lookup نوع 3, feature `curs`):** پیاده نشده؛ احتمالاً علت اصلی اختلاف با HarfBuzz روی Amiri و Noor_e_Quran (در مقایسه‌ی بخش 9 فقط 60 تا 80 درصد
      متن‌ها یکی‌اند)؛ تأیید نشده، اول `curs` را در HarfBuzz خاموش کن و ببین اختلاف می‌ماند یا نه (اختلاف Amiri با `curs` خاموش هم ماند، پس شاید علت دیگری هم باشد).
    - **IRANSans:** 21 از 300 متن با HarfBuzz فرق دارد (حداکثر 150 واحد)، نمونه: `چكّْ فّغٌجٌپٍ جًشْژُ`؛ علت بررسی نشده (حدس: ترتیب marks از `mark_order` برای marks خاص، یا `abvm`/`blwm`).
    - kerning لاتین هنوز از `face.glyph_kerning` می‌آید (اسکن همه‌ی lookupهای نوع 2 بدون انتخاب feature/script، فقط جفت glyph، بدون context)؛ برای یکدستی می‌توان آن را هم به
      `positioner` برد (script `latn`/`DFLT`)، ولی `glyph_kerning(l, r)` به‌دلیل context نمی‌تواند نتیجه‌ی کامل بدهد (در docs هم بنویس).
    - ZWNJ، متن لاتین داخل خط عربی (حالا `kerned` فقط برای glyph عربی است)، و `font::kerning(char32_t...)` قدیمی دست نخوردند.
    - چند فونت واقعی (Vazirmatn) را به `tests/` اضافه نکردم؛ فقط Regular که از قبل در `data/fonts` بود در تست HarfBuzz استفاده می‌شود.
    - **issueهایی که کاربر خواست متنشان نوشته شود (برای GitHub):** (1) kerning زمینه‌ای Vazirmatn (انجام شد)، (2) ترتیب marks (انجام شد)، (3) mark بی‌حرف (انجام شد). هر سه در بخش 9 حل شدند؛ متن‌ها فقط در چت بودند.
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

## این دور: `data/`، مثال‌ها، تست‌ها (تمام شد، commit نشده)

درخواست کاربر: یک پوشه‌ی `data` در ریشه با چند فونت و تصویر (مثل cameraman و lena) که مثال‌ها و تست‌ها با آن‌ها کار کنند.

- **`data/`** (راهنما و لایسنس هر فایل: `data/README.md`؛ `NOTICE.md` هم بخش دارد): `images/` شامل `camera.png` (CC0)، `astronaut.png` (NASA، public domain)،
  `chelsea.png` (CC0)، `rocket.jpg` (JPEG واقعی، public domain)، `badge.png` (RGBA، ساخته‌شده با libstb توسط `data/make_badge.py`)؛ `fonts/` شامل
  Vazirmatn Regular و Bold (OFL 1.1، فایل‌های دست‌نخورده‌ی upstream) و `OFL.txt`. **Lena عمداً نیست** (عکس Playboy است و در مخزن MIT قابل
  توزیع نیست؛ cameraman قدیمی هم همین شک را دارد و scikit-image عکس CC0 جایگزین کرده؛ `camera.png` همان است). `tests/data/` همان فونت‌های ساختگی
  برای عددهای دقیق است و عوض نشد. `data/` داخل wheel نمی‌رود.
- **مثال‌ها:** همه‌ی مثال‌های پایتون (01 تا 11 و 13 تا 15) و 4 نوت‌بوک از `data/` می‌خوانند (به‌جای گرادیان ساختگی و فونت سیستم/matplotlib). برنامه‌های C++ بدون
  آرگومان اجرا می‌شوند (`LIBSTB_DATA_DIR` را `examples/cpp/CMakeLists.txt` تعریف می‌کند و خروجی به `./output/` می‌رود). **جدید:** C++ `font_text`، `vector_text`،
  `limits_and_zlib`، `logging_and_threads`؛ پایتون `13_font_glyphs_and_shape.py`، `14_untrusted_input_and_zlib.py`، `15_logging_threads_simd.py`.
  `05_font_render.py` هم متن کهنه‌ی «stb_truetype» را از دست داد. همه با CMake بیلد (صفر warning) و اجرا شدند.
- **تست‌ها:** `tests/python/sample_data.py` (مسیرها)، `test_sample_data.py`، `test_sample_fonts.py`، `tests/cpp/test_sample_data.cpp` (+ `LIBSTB_DATA_DIR` در
  `tests/cpp/CMakeLists.txt`). نکته‌ها از اجرا: ریسایز پیش‌فرض در نور خطی است (روی عکس 0.6 تا 6.2 سطح روشن‌تر از میانگین مقادیر؛ `Resizer(srgb=False)` میانگین را حفظ می‌کند)؛
  JPEG گریِ نوشته‌شده سه‌مؤلفه‌ای است؛ شکل دادن فارسی با HarfBuzz روی Vazirmatn واقعی برای 588 متن تصادفی بدون هیچ اختلافی در گلیف و advance است.
- **باگ واقعی پیدا و درست شد** (`bind_image.cpp`): پیام `nanobind: leaked 1 instances!` آخر اسکریپت وقتی `shared = np.from_dlpack(img)` و
  `back = Image.from_dlpack(shared)` در globals بود. علت: `KeepAlive` فقط وقتی `Py_IsInitialized()` درست بود آزاد می‌کرد و آن موقع شروع finalization غلط است. حالا
  `nb::is_alive()`. تست: `test_gc.py::test_no_leak_report_at_exit_for_an_image_over_an_array_made_from_an_image` (روی ماژول قدیمی fail می‌شد).
  **هنوز باز:** هر کلاسی که در `__main__` تعریف شود و جای دیگری (مثلاً handler پایتون `logging`) نگه داشته شود، globals را زنده نگه می‌دارد و nanobind هنگام خروج
  می‌گوید Imageهای globals leak شده‌اند؛ مثال 15 از `logging.handlers.BufferingHandler` استفاده می‌کند تا این شکل پیش نیاید. راه کلی `nb::set_leak_warnings(false)` است (استفاده نشد).
- **دو اشتباه مستند اصلاح شد:** (1) `pixel_height` در `font::metrics`/`draw_text` ارتفاع descent تا ascent است (hhea)، نه em (هدر «em-height» می‌گفت)؛ برای هماهنگی با
  `draw_text` مقیاس خط‌های برداری `size / (ascent - descent)` است (نه `units_per_em`)؛ `font.hpp`، `guide.md`، docstring `design_metrics` و مثال‌ها اصلاح شدند.
  (2) ترتیب `matrix`: هر فراخوانی روی نقطه اول اعمال می‌شود، پس «scale، بعد move» یعنی `translate(...).scale(...)` (دو قطعه‌ی README برعکس بود).

## پایتون: API فونت (تمام شد، commit نشده)

API تأییدشده توسط کاربر (طبق قاعده‌ی «اول API را توصیف کن»): روی `libstb.Font`، همه در واحد فونت با y رو به بالا.
`glyph_index(char)`، `glyph_count`، `glyph_info(glyph) -> GlyphMetrics`، `design_metrics -> DesignMetrics`، `outline(glyph)`
(همان tuple‌های `Outline`، پس `Outline(font.outline(g))` کار می‌کند)، `glyph_kerning(left, right, mode="automatic")`،
`Font.face_count(source)`، `shape(text, *, direction="auto", kerning="automatic", ligatures=True) -> list[ShapedGlyph]`.

- کد: `src/bindings/python/bind_font.cpp` (+ `command_tuple` که از `bind_outline.cpp` بیرون آمد و در `binding.hpp` اعلام شد، تا کپی نشود)،
  `libstb/font.py` (سه NamedTuple)، `libstb/__init__.py` (export). تست: `tests/python/test_font.py` (بخش‌های «glyph-index API»، «Font.shape»
  و «a vector renderer written in Python»؛ از جمله برابری پیکسلی `fill_outline` با `draw_text`).
- خطا: اندیس منفی یا بزرگ‌تر از 2**32-1 `ValueError`؛ اندیس بعد از گلیف‌ها «هیچ» است (outline خالی، metrics صفر) نه خطا، مثل C++.
- GIL: فقط `shape` برای متن 512 بایت UTF-8 یا بیشتر آزاد می‌کند (اندازه‌گیری: 0.2 تا 0.4 میکروثانیه به‌ازای هر بایت)؛ `glyph_index`،
  `glyph_info`، `outline`، `glyph_kerning` کار چند میکروثانیه‌ای‌اند و GIL را نگه می‌دارند.
- مستندات: `docs/guide.md` («From Python»)، `CHANGELOG.md`.

## پایتون: `Image.to_pil()` بدون کپی برای L و RGBA (تمام شد، commit نشده)

به درخواست کاربر («خودکار و بدون کپی»): `to_pil()` برای مود `L` و `RGBA` با `PILImage.frombuffer(..., "raw", mode, 0, 1)` حافظه‌ی تصویر را
به اشتراک می‌گذارد (اندازه‌گیری: 1 ms برای 1080p RGBA و 24 ms برای 4000x3000 کپی می‌شد، حالا حدود 5 میکروثانیه). `RGB` و `LA` هنوز کپی
می‌شوند چون Pillow آن‌ها را 4 بایت به‌ازای هر پیکسل نگه می‌دارد. تصویر Pillow `readonly` است و در اولین نوشتن کپی می‌شود، پس هرگز در `Image`
نمی‌نویسد؛ ولی تغییر پیکسل‌های `Image` (از `numpy()` یا آرایه‌ای که wrap کرده) در تصویر Pillow دیده می‌شود. `img.to_pil().copy()` مستقل است.
هیچ پارامتر جدیدی اضافه نشد (بحث `copy=` کنار گذاشته شد). `from_pil` همچنان کپی می‌کند (Pillow فقط `tobytes()` می‌دهد). تست‌ها:
`tests/python/test_pillow_convert.py`. `move` (null کردن منبع) عمداً نیست: Pillow مالکیت حافظه‌ی بیرونی را نمی‌گیرد و نگه داشتن مرجع کافی است.

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

## 2026-11-09: بررسی leak و issue #4 (فقط بررسی، کدی عوض نشد)

- **leak:** روی بیلد لینوکس (CPython 3.12) ۵۰۰۰ تکرار از copy، DLPack، resize، encode، to_pil، Font.shape، draw_text، Canvas و مسیر خطا: RSS ثابت (36.6 MB). پیام
  `nanobind: leaked ...` در خروج فقط با thread daemon که هنوز Image نگه داشته بازتولید شد (رفتار CPython). چهار شکل دیگر (handler در `__main__`، handler نگه‌دارنده‌ی Image،
  BufferingHandler با DLPack، cycle ساده) پیام ندادند؛ جمله‌ی «هنوز باز» بالا در بخش leak احتمالاً قدیمی است (مثال 15 را اجرا نکردم).
  دو جای عمدی: `bridge_state` در `bind_logging.cpp` هرگز آزاد نمی‌شود و `KeepAlive` بدون interpreter delete نمی‌کند. پیشنهاد (تأیید نشده): وصل کردن `bridge_state` به teardown ماژول و تست «خروج بدون پیام».
- **issue #4** (novasvg نسخه‌های محلی دارد چون libstb ندارد): ۱) Porter-Duff و `blend_mode::exclusion`، ۲) رنگ CSS (۱۴۸ نام، rgb/hsl، `color::try_parse`)، ۳) `matrix::map_rect` و `map` آرایه‌ای،
  ۴) `outline::arc_to`، ۵) `outline::tight_bounds`. API پیشنهادی در چت نوشته شد و **هنوز تأیید کاربر نیامده**؛ پیاده‌سازی نشده. ترتیب پیشنهادی: ۵، ۳، ۴، ۲، ۱.
