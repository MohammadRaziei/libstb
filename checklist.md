# libstb: کارهای باقی‌مونده

> کانتکست: `github.com/mohammadraziei/libstb` (هسته‌ی C++ با namespace `stb` و بایندینگ Python روی
> stb). این فایل خلاصه‌ی چیزیه که انجام شده و چیزیه که مونده. هر مورد انجام‌نشده یا تست‌نشده صریح
> نوشته شده. push رو کاربر می‌زنه (من دسترسی ندارم).

## انجام‌شده

- [x] **نوشتن متن روی تصویر.** `font::draw_text` (C++) و `Image.draw_text` (Python): متن چندخطی UTF-8،
      رنگ با آلفا (شفافیت)، برش خودکار در لبه‌ها، روی تصویر ۱ تا ۴ کاناله. تصویر ورودی تغییر نمی‌کنه.
      از `image::composite` استفاده می‌کنه، پس حلقه‌ی blend تکراری نداریم.
- [x] **`stb::color`** (`include/stb/color.hpp`): کلاس `Color` از novasvg، با namespace `stb` و
      نام‌های snake_case (`color`, `color_blend`, `invalid_color_error`, `r()`, `set_r()`,
      `from_string`, ...). بلوک engine-facing و ثابت‌های `Color::Black/White/Transparent` حذف شدن.
      توی Python رنگ می‌تونه رشته (`"red"`, `"#ff8800"`)، عدد یا tuple باشه. ۲۰ تست از novasvg پورت شد.
- [x] **`canvas` (C++) و `Canvas` (Python)**: چند متن و شکل روی یک کپی از تصویر.
      `image::composite_in_place` و `font::draw_text_in_place` اضافه شدن؛ `composite()` و
      `draw_text()` الان کپی + نسخه‌ی درجا هستن.
      API (C++ و Python دقیقاً یکی، `Canvas(img)`): `c.add_text(متن, x, y)` یه شیء متن برمی‌گردونه
      (`canvas::text_item`، `CanvasText`) با `.font() .size() .color() .align() .bg_color() .padding()`.
      روی خود canvas `set_font`/`set_size`/`set_color`/`set_align` پیش‌فرضِ متن‌های بعدیه (موقع
      `add_text` کپی می‌شه). کشیدن lazy هست: همه‌ی متن‌ها توی `finish()` به ترتیب اضافه‌شدن کشیده
      می‌شن، پس خطای «بدون فونت» هم همون‌جا میاد. بعد از `finish()` canvas و متن‌هاش
      `invalid_argument` (`ValueError`) می‌دن ولی dangling نمی‌شن. `draw_text` فوریِ canvas و
      `result()` حذف شدن. هر متن دوباره از `c` شروع می‌شه (به هم پایپ نمی‌شن). `finish()` توی هر دو `c.finish()` هست. `Canvas`/`CanvasText` و `Image.draw_text` توی Python با nanobind بایند شدن (`bind_font.cpp`)،
      نه کد Python. خروجی C++ و Python بایت به بایت یکیه: دو تست (`test_canvas_matches_*_golden_hash`) همون صحنه رو می‌سازن و باید به یک FNV-1a برسن.
      پس‌زمینه: جعبه‌ی layout متن + padding، به اندازه‌ی تصویر clip می‌شه (padding بزرگ حافظه‌ی
      زیاد نمی‌گیره). آلفای رنگ رعایت می‌شه.
      شکل‌ها: `add_rect(x,y,w,h)` (با `.radius()`)، `add_ellipse(x,y,w,h)`، `add_line(x1,y1,x2,y2)`
      با `.fill() .no_fill() .stroke(عرض[, رنگ])`. پیش‌فرض: رنگ canvas (پر برای rect/ellipse، خط با
      عرض ۱). لبه‌ها صافن (anti-aliased، SDF)، حاشیه داخل جعبه کشیده می‌شه و خط از مرکز پیکسل‌های
      دو سرش می‌گذره (عرض فرد crisp می‌مونه، عرض زوج بین دو ردیف می‌افته). کد توی `src/draw/shape.cpp`
      با `-ffp-contract=off` کامپایل می‌شه که روی ARM هم همون بیت‌ها دربیاد.
      بیضی با تخمین مرتبه‌ی اول فاصله کشیده می‌شه: روی محورها ضخامت حاشیه دقیقه (اختلاف تا ۰.۰۴
      پیکسل اندازه‌گیری شد)، بین محورها اندازه‌گیری نشده.
      دو golden hash (متن، و شکل+متن) بین تست C++ و Python مشترکن؛ ثابت‌هاشون روی ویندوز/macOS هنوز
      تأیید نشده (متن با stb_truetype و کامپایلرهای دیگه ممکنه آخرین بیت رو عوض کنه).
- [x] **متن راست‌به‌چپ (فارسی/عربی/اردو/عبری)** توی `draw_text`/`measure`/`render` و `canvas`، بدون
      وابستگی. پایپ‌لاین واحد در دامنه‌ی glyph-id: `src/font/shaper/text_shaper.*` (جوینینگ → glyph → GSUB → bidi
      → آینه‌ای‌کردن)، روی چهار هسته‌ی مستقل: `arabic.*` (جدول‌های حروف، تولیدشده با
      `tools/gen_arabic_shaping.py` از `unicodedata`، Unicode 15.0.0)، `bidi.*` (UAX #9 بدون
      embedding/isolate)، `otl.*` (خواندن امن جدول‌ها: coverage، class def، GDEF) و `gsub.*` (lookupها
      به‌صورت کلاس‌های polymorphic).
      **GSUB:** نوع‌های ۱ تا ۷ (single، multiple، alternate، ligature، context و chaining context در هر سه
      قالب، extension)، lookup flag (ignore marks/base/ligatures، mark attachment، mark filtering set) با
      GDEF، stageها به ترتیب مدل عربی (`ccmp/locl` → `isol` → `fina` → `medi` → `init` → `rlig` → `calt`
      → `liga/clig/rclt`) با mask هر شکل. فونتِ بدون GSUB ولی با Presentation Forms در `cmap` از همان
      مسیر قدیمی کار می‌کنه. `API`ی `shape_for_display` رشته‌ایه و با یک منبع هویتی روی همین هسته اجرا می‌شه.
      پارس GSUB تنبل و یک‌بار (حدود ۰٫۱ms) در اولین متن RTL هر فونت؛ باز کردن فونت کندتر نشد.
      **راستی‌آزمایی:** glyph idها با HarfBuzz (uharfbuzz) روی ۳۰۰۰×۲ رشته‌ی تصادفی برای هر کدوم از
      Vazirmatn و Noto Naskh Arabic و Noto Sans Arabic یکی‌ان (۰ تفاوت؛ اعراب به‌صورت مجموعه چون ترتیب
      ما «بعد از حرف» ه). همین مقایسه چند باگ واقعی گرفت که تست‌های واحد نگرفته بودن: همزه (U+0621) حرف
      غیرچسبنده‌ست نه راست‌چسب، شکل منفرد باید glyph اسمی بمونه نه Presentation Form، glyphِ بی‌کلاس GDEF
      علامت حساب نمی‌شه، و آفست coverage در context قالب ۳ (که فیلتر سرعت رو خراب کرده بود).
      **سرعت:** حدود ۲۲۰ns برای هر نویسه (۴۳۸ نویسه = ۹۷µs)، حدود ۲٫۴ برابر سریع‌تر از Pillow+raqm.
      فیلتر bitmap برای هر lookup (glyphهایی که می‌تونه از اون‌ها شروع کنه) سرعت رو ۲٫۸ برابر کرد.
      **امنیت:** فونت ورودیِ نامطمئنه. همه‌ی خواندن‌ها range-check هستن، سقف رشد glyph (۱۶ برابر متن + ۶۴) و
      بودجه‌ی عملیات (بر حسب glyph × lookup) داریم. fuzz با AddressSanitizer+UBSan روی ۵۰٬۰۰۰ فونت
      خراب، بدون crash؛ این fuzz یک فونت پیدا کرد که ۵۰ نویسه رو به ۳۰۰٬۰۰۰ glyph می‌رسوند (قبل از سقف‌ها).
      تست‌ها: `tests/cpp/test_gsub.cpp` (۱۳ تست با جدول‌های دست‌ساز، هر نوع lookup و قالب، بعلاوه‌ی جهش
      ۸۰۰ فونت خراب)، golden مرجع HarfBuzz در C++ و Python، و مقایسه‌ی زنده با HarfBuzz در Python (اگر
      `uharfbuzz` نصب باشه). دو فونت subset از Vazirmatn (OFL): `libstb-fa-test.ttf` (بدون GSUB) و
      `libstb-fa-gsub-test.ttf` (GSUB ولی بدون Presentation Forms)، ساخته‌شده با
      `tests/data/make_persian_font.py`.
      اعراب (harakat) با قاعده‌ی ساده جا می‌گیرن، نه با GPOS: وسط‌چین روی ink حرف قبلی، بالا یا پایینِ
      bbox اون (کسره و هم‌خانواده‌هاش پایین)، شدّه اول، و بدون عرض. تست:
      `test_marks_take_no_room_and_sit_over_or_under_the_letter`.
      `Font.shape_glyphs` (C++: `font::shape_glyphs`) glyph idهای انتخاب‌شده رو برمی‌گردونه (تشخیص/تست).
      **محدودیت‌ها (صادقانه):**
      - GPOS برای متن عربی: featureهای `kern`، `dist`، `mark`، `mkmk`، `abvm`، `blwm` (lookup نوع ۱، ۲، ۴، ۵، ۶، ۷، ۸ و extension)
        اجرا می‌شن (`src/font/shaper/gpos.*`). cursive (نوع ۳) و device/variation tables نه. اعراب فقط وقتی anchor ندارن با
        قاعده‌ی ساده جا می‌گیرن. kerning لاتین هنوز جفت‌گلیف از `face` ه. با HarfBuzz روی Vazirmatn (هر سه وزن)، Scheherazade و Lateef
        دقیق یکی‌ه؛ Amiri و Noor_e_Quran نه (احتمالاً `curs`). فونت‌هایی که شکلشون به cursive وابسته‌ست (بعضی نستعلیق‌ها) خراب دیده می‌شن.
      - lookup نوع ۸ (reverse chaining) خونده نمی‌شه؛ فقط langsys پیش‌فرض اسکریپت `arab` (یا `DFLT`)،
        یعنی `locl` بر اساس زبان نیست؛ `fin2/fin3/med2` (سریانی) و `rtlm` نه.
      - ZWNJ حذف می‌شه (HarfBuzz یک glyph با عرض صفر نگه می‌داره؛ تصویر یکیه).
      - `font::advance`/`kerning`/`render_glyph` روی یک code point هستن و shaping ندارن.
      - تست‌نشده: عبری و حروف مخصوص اردو، فونت‌های دیگه جز سه‌تای بالا، متن با کنترل‌های صریح bidi
        (embedding/isolate نادیده گرفته می‌شن)، فونت OpenType-CFF (.otf)، و B Nazanin.
- [x] **`text_align` (left, center, right)** برای `draw_text` و `canvas` (`align=` توی Python). با center/right،
      x مرکز یا لبه‌ی راستِ هر خط می‌شه. `measure` از قبل وجود داشت (`font::measure`, `Font.measure`).
- [x] README (خط ۳) و توضیح PyPI (`pyproject.toml`) حالا zlib و نوشتن متن رو هم می‌گن.
- [x] مستندات: بیلد `stb_docs` با صفر هشدار (هشدارهای Doxygen توی `color.hpp` و docstring اسفینکس درست شد)؛
      رنگ prompt (`>>>`) توی بلوک doctest دارک اسفینکس خوانا شد.
- [x] README، `docs/guide.md`، `docs/cpp/mainpage.md`، `docs/python/source/font.rst` و
      `examples/python/05_font_render.py` آپدیت شدن.
- [x] **`image::premultiplied()` / `unpremultiplied()`** (کپی جدید، بایت‌ها R,G,B,A، بدون فلگ؛ rgba و gray_alpha؛ Python هم). کرنل مشترک با توابع ARGB.
- [x] **`premultiplied_image`** (issue 9 novasvg): نوع جدا روی پایه‌ی مشترک `detail::image_base`؛ `from_image`/`to_image`/`wrap`/`copy`؛ Python هم.
- [x] **شکل‌دهی متن LTR** (issue 8 novasvg): GSUB/GPOS برای latn/grek/cyrl در `font::shape`؛ مثل HarfBuzz روی متن تک‌اسکریپتی؛ فونت و تست مصنوعی.
- [x] **تبدیل RGBA مستقیم ↔ ARGB32 premultiplied (issue #1).** دو تابع آزاد `rgba_to_premultiplied_argb` و
      `premultiplied_argb_to_rgba` توی `include/stb/image.hpp`، با همون امضای issue (`dst, dst_stride, src,
      src_stride, width, height`؛ strideها بایتی؛ درجا مجازه). کرنل‌ها توی `kernels.hpp` (`premultiply_row`) و
      با همون dispatch فعلی (avx2 / sse2 یا neon / scalar). بدون شاخه: ضرب و تقسیم بر ۲۵۵ با
      `(v + 1 + (v >> 8)) >> 8`، جهت معکوس با تقسیم float (دقیقه چون `c*255 < 2^24`). بلوک‌های ۳۲پیکسلیِ
      کاملاً مات فقط R و B رو جابه‌جا می‌کنن. مقدار بالای ۲۵۵ (فقط ورودی نامعتبر `c > a`) مثل ذخیره‌ی
      `uint8_t` wrap می‌شه؛ **فرضِ من اینه که مرجع novasvg هم همینه و تأیید نشده**.
      تست: `tests/cpp/test_premultiply.cpp` (همه‌ی ۶۵۵۳۶ جفت (c, a) روی هر backend، درجا و غیردرجا، با
      padding و آدرس نامرتب؛ با دو جهش عمدی در کرنل هم چک شد که تست خراب‌کاری رو می‌گیره).
      بنچمارک: `cmake --build <build> --target stb_bench_premultiply` (۴۰۹۶×۴۰۹۶، سه ترکیب آلفا، با سطر
      memcpy به‌عنوان سقف). روی این ماشین (یک هسته، نویزی) avx2 حدود ۱٫۸ تا ۶٫۵ برابر حلقه‌ی مرجع و sse2
      حدود ۱٫۳ تا ۴٫۶ برابر؛ روی ورودی مات نزدیک سقف حافظه.
- [x] **Thread هوشمند (`stb::thread_count / set_threads / thread_scope`، پایتون `threads(n)`، env
      `LIBSTB_THREADS`).** تصمیم داخل `src/util/parallel.hpp`: `threads_for(work_ns)` هر thread اضافه رو فقط
      وقتی می‌سازه که حداقل ~۲۵۰µs کار داشته باشه (و هرگز بیشتر از سقف کاربر)؛ کار کوچیک روی thread صدازننده
      می‌مونه و هیچ thread‌ای ساخته نمی‌شه. thread فقط در طول فراخوانی زنده‌ست (بدون pool، با `fork` سازگار).
      فقط `resize` ازش استفاده می‌کنه (split ردیف‌های خروجی خود موتور؛ `stbir_resize_extended_split` رو از
      upstream برگردوندم). خروجی بایت‌به‌بایت با تک‌thread یکیه (تست ۱/۴/۷ thread روی هفت فیلتر × چهار
      کانال × پنج شکل، و زیر ThreadSanitizer هم بدون هشدار). تخمین کار از اندازه‌گیری تک‌هسته‌ی همین ماشین:
      حدود ۱٫۲ تا ۲٫۹ ns به‌ازای هر نمونه؛ در کد کف محافظه‌کارانه‌ی ۱٫۰ (و ۰٫۳ برای nearest) گذاشتم.
      **اثر واقعی چندهسته‌ای اندازه‌گیری نشده** (ماشین من یک هسته داره): ثابت‌ها بعداً روی ماشین چندهسته‌ای
      تنظیم بشن.
- [x] **API گلیف-ایندکس برای رندر وکتور (گام الف درخواست novasvg).** `font::outline(g)` (و فرم بافر
      `outline(g, out)`) به‌صورت `outline_command` در واحد فونت (`quad_to` برای TrueType، `cubic_to` برای CFF،
      `close` ساخته‌شده؛ y رو به بالا)، `glyph_index`، `glyph_count`، `glyph_info`، `design_metrics`،
      `glyph_kerning(l, r, kerning_mode)` و `font::face_count`. روی `truetype::face` ساخته شد (فقط
      `units_per_em`، bbox فونت، `num_glyphs`، `kern_table_advance`، `font_count` اضافه شد). اندیس بیرون از
      بازه exception نمی‌ده. تست: `tests/cpp/test_font_outline.cpp` (۱۷ تست، با فونت‌های مصنوعی جدید: منحنی +
      composite + GPOS/kern، CFF و `.ttc`؛ دو جهش عمدی گرفته شد). بخش ب (`font::shape`) پایین انجام شد.
- [x] **WOFF1.** `font::open` / `font::from_memory` (و `Font.open` در پایتون) فایل `.woff` را در حافظه با zlib خودمان به sfnt باز
      می‌کنند (`src/font/woff.cpp`، `woff.hpp`)؛ `face_count` برای آن ۱ است؛ WOFF2 با `decode_error` و پیام روشن رد می‌شود.
      فایل خراب `decode_error`، فونتی که بیش از ۵۱۲ MiB باز شود `limit_error`، و حافظه به‌حرف فیلد طول رزرو نمی‌شود. تست:
      `tests/cpp/test_woff.cpp` (۱۵ تست: جدول‌های باز‌شده با فونت اصلی بایت‌به‌بایت برابرند، فونت‌های ساخته‌شده توسط خود
      تست با ترتیب و ذخیره‌ی متفاوت، فایل‌های خراب، WOFF2، لاگ) و دو تست در `tests/python/test_font.py`. فونت‌های تست را
      `tests/data/make_woff_test_fonts.py` می‌سازد و برابری جدول‌ها را خودش چک می‌کند. دو جهش عمدی (حذف بررسی هم‌پوشانی و
      بررسی `origLength`) گرفته شد؛ جهش هم‌پوشانی اول گرفته نشد چون تست قبلی به دلیل دیگری رد می‌شد، برای همین تستی اضافه شد که هر دو جدول جدا سالم باز می‌شوند.
- [x] `tests/python/test_logging.py`: سه تست که numpy می‌خواهند حالا با `pytest.importorskip("numpy")` داخل خودشان skip می‌شوند
      (CI ویندوز ARM numpy نداشت و سه تست fail شد).
- [x] تست: ۴۵۳ تست C++ پاس (قبلاً ۴۱۱). Python: ۱۴۱۶ پاس (قدیمی؛ این دور شمارش نشد)، ۴ skip (`pytest_memray`، `torch` و `uharfbuzz` نصب نبودن، و یک مورد BMP). `ctest` سه تا از سه.
- [x] کارهای بالا (به‌جز `align` و مستندات اخیر) توی `053fe98` روی `origin/master` هستن؛ بقیه کامیت‌های محلی با
      اسم placeholder `Claude` هستن (اگه خواستی `git commit --amend --reset-author`).

## قبل از هر چیز دیگه

- [ ] **push و چک CI.** `git push origin master`؛ اگه چیزی push شده بود قبلش `git pull --rebase`.
      ببین job `build-docs` سبز می‌شه و `Canvas`/`Image.draw_text` توی Sphinx و Doxygen درست دیده
      می‌شن (سایت رو من بیلد نکردم). سایت فقط با push به `master` ساخته می‌شه (`orchestrator.yml`).
- [ ] **ویندوز/macOS/Clang تست نشده.** همه‌چی فقط روی لینوکس/GCC اجرا شد. `color.hpp` از
      `std::clamp` استفاده می‌کنه (ماکروهای `<windows.h>` فقط `min`/`max` رو می‌گیرن)، ولی تایید با CI میاد.

## قابلیت‌ها

- [x] **GPOS برای kerning و mark، `glyph_kind`، `shape(text, begin, end)`** (commit نشده). `otl::positioner` (`gpos.hpp`، `gpos.cpp`)، `shaped_glyph::kind`
      (`normal`، `notdef`، `line_break`)، شکل‌دهی بخشی از متن به‌عنوان جزئی از کل (`Font.shape(..., begin, end)` در پایتون، بایت). ترتیب marks مثل HarfBuzz
      (`order_marks`) و mark بی‌حرف بدون عرض. تست: 11 تست جدید در `test_font_shape.cpp`، فونت `libstb-fa-gpos-test.ttf`، و تست HarfBuzz روی Vazirmatn در `test_sample_fonts.py`.
      C++ 453 تست پاس. **باز:** cursive (نوع ۳)، اختلاف IRANSans/Amiri/Noor_e_Quran با HarfBuzz، kerning لاتین از `positioner`، اجرا روی CI ویندوز/macOS. جزئیات: `handoff.md` بخش 9 و 12.
- [x] shaping با cluster: `font::shape(utf8, shape_options)` → `shaped_glyph {glyph, cluster (آفست بایت), x_advance, x_offset, y_offset}` به
      واحد فونت و y رو به بالا؛ `shape_options {direction, kerning, ligatures}` (ligatures=false: `liga/clig` بسته، `rlig` و joining نه). `cluster`
      از bidi، mirror، joining و ligatureها رد می‌شود (`glyph_record::cluster`). **روی لینوکس اجرا و پاس (CI هنوز نه).** تست:
      `tests/cpp/test_font_shape.cpp`. باز: GSUB برای متن لاتین (الان فقط راست‌به‌چپ). بایندینگ پایتون (`Font.shape`) و GPOS انجام شد.
- [ ] WOFF2 (Brotli لازم؛ احتمالاً نه) فقط اگه novasvg خودش decode نکنه. (WOFF1 تموم شد.)
- [ ] thread برای کارهای دیگه (blend با حالت‌های float، کدگذاری JPEG) فقط بعد از اندازه‌گیری روی ماشین
      چندهسته‌ای؛ کارهای حافظه‌محور (premultiply، flip، convert) عمداً بدون thread‌ن. بایندینگ‌های تصویر پایتون
      هنوز GIL رو موقع کار سنگین آزاد نمی‌کنن (فقط فونت‌ها می‌کنن).
- [ ] `premultiply`: backend sse2 دستی نوشته نشده (فقط vectorize خودکار؛ روی ورودی نیمه‌شفاف جهت مستقیم
      حدود ۱٫۳ برابر مرجعه). NEON، arm64، MSVC و ویندوز اجرا/اندازه‌گیری نشدن.
- [ ] فرمت اختیاریِ premultiplied توی `pixel_format` (رندر مستقیم novasvg توی حافظه‌ی `image` بدون تبدیل) فقط
      اگه بنچمارک واقعیِ novasvg نشون داد این مرز هنوز مهمه؛ تصمیم گرفتیم ذخیره‌ی پیش‌فرض `image` همون
      straight RGBA بمونه.
- [ ] اگه گزینه‌ها زیاد شدن (فاصله‌ی خطوط، حداکثر عرض با شکستن خط، سایه، outline، anchor عمودی): یه struct گزینه
      با مقدار پیش‌فرض (`{.color = ..., .align = ...}` توی C++ و kwargs توی Python)، نه بیلدر.
- [ ] `color` توی Python فقط رشته/عدد/tuple قبول می‌کنه و کلاس مستقل نیست. اگه لازم شد expose بشه.
- [ ] هزینه‌ی `canvas` در برابر چند بار `draw_text` **اندازه‌گیری نشده** (و تست «بدون کپی اضافه»
      با اشاره‌گر داده حذف شد، چون `result()` دیگه نیست). تا عدد نداریم ادعای سرعت ننویس.
- [ ] تصمیم: دو نسخه‌ی `Color` (novasvg با CamelCase، libstb با snake_case). ببین `checklist.md`
      توی novasvg (بخش «Color مشترک با libstb»).

## سایت و مستندات

- [x] دارک تم: هیرو landing توی دارک سبز برنده (توکن `--hero-bg` توی هر دو تم یکیه) و Sphinx و Doxygen توکن‌های
      مشترک `libstb-docs.css` رو می‌گیرن. این رو با اسکرین‌شات ۱۲۸۰ پیکسل (روشن و تاریک، landing و مرجع Python و
      C++) چک کردم؛ هیچ‌کدوم هیرو ندارن که سبز بشه.
- [ ] `tests/genhtml.css` (گزارش پوشش) رو بررسی نکردم و دارک تمش چک نشده.
- [ ] اسکرین‌شات‌ها با فونت‌های fallback گرفته شدن (Google Fonts توی سندباکس دسترس نیست)، پس Archivo و JetBrains Mono
      واقعی دیده نشدن. موبایل هم چک نشده.
- [ ] متن‌های «فقط تصویر»: `docs/guide.md` و `docs/python/source/index.rst` هنوز بررسی نشدن.
- [ ] لوگوی PNG، و بررسی موبایل **گزارش بنچمارک** (فقط landing روی ۳۹۰ پیکسل چک شده بود).
- [ ] فایل‌های قالب Sphinx به `DEPENDS` اضافه بشن (الان بعد از ویرایش قالب باید `build/docs/out`
      پاک بشه یا `conf.py` touch بشه).
- [ ] لینک خراب `python/html/_modules/index.html` به `libstb/libstb_py.html` (ماژول کامپایل‌شده)؛ ریشه‌ش
      بررسی نشده.

## بنچمارک و انتشار

- [ ] جدول اندازه‌ی نصب هنوز libstb **0.5.0** رو نشون می‌ده؛ `pip-size` باید برای نسخه‌ی جدید دوباره
      اجرا بشه. بنچمارک روی دستگاه واقعی (داده‌ی فعلی از سندباکس ۱ هسته‌ای، ۲۰۲۶-۱۰-۱۵).
- [ ] بنچمارک zlib توی `benchmarks/` (طبق اندازه‌گیری قبلی: compress سریع‌تر، inflate نه). بدون
      اندازه‌گیری توی صفحه ننویس.
- [x] ورودی `CHANGELOG.md` برای 0.8.0 نوشته شد (`draw_text`، `color`، `canvas`، RTL/GSUB).
- [ ] هنوز مونده: `LIBSTB_VERSION_MINOR` توی `include/stb.h` (الان 7)، تگ `v0.8.0` و انتشار. با `python version.py` انجام می‌شه.
- [ ] اگه خواستی، اسم‌های داخلی `LIBSTB_*` یکدست بشن (فقط تغییر اسمه).

## دام‌ها

- `composite_in_place` روی تصویری که حافظه‌ی بیرونی رو wrap کرده (مثلاً numpy) همون حافظه رو تغییر
  می‌ده. `canvas` همیشه روی یه کپی کار می‌کنه، پس امنه.
- `draw_text` فونت ناشناس رو لود نکن: stb_truetype بررسی مرز نداره (فقط فونت مورد اعتماد).
- گزارش بنچمارک تک‌فایله‌ست و بعد از هر تغییر قالب باید دوباره ساخته بشه (دستورش توی هندآف قبلی).

## issue #2: rasterizer و stroker برداری (novasvg)

- [x] `stb/outline.hpp` + `src/draw/outline.cpp`: `matrix`, `outline` (builder، شکل‌ها، `add_outline`/`add_commands`، `transform`، `dashed`)؛
      `point`/`outline_op`/`outline_command` از `font.hpp` به اینجا آمدند.
- [x] `stb/raster.hpp` + `src/draw/raster.cpp`: `rasterize_fill`/`rasterize_stroke` (span ها)، `fill`/`stroke` روی `image`،
      `fill_rule`، `stroke_style`، `draw_options`، `span_sink`.
- [x] `src/draw/raster/`: `fixed` (ریاضی ثابت‌نقطه)، `fixed_outline`، `gray_raster` (پورت ftgrays)، `stroker` (پورت ftstroke)، `dash`.
- [x] `canvas::add_outline` (`outline_item`).
- [x] پایتون: `Matrix`, `Outline`, `Image.fill_outline/stroke_outline`, `Outline.fill_mask/stroke_mask`, `Canvas.add_outline`/`CanvasOutline`.
- [x] تست دقیق در برابر plutovg (319 صحنه، `tests/cpp/test_raster_reference.cpp`)، تست رفتاری C++ و پایتون.
- [x] `NOTICE.md` (FTL/plutovg)، `CHANGELOG.md`، `README.md`، `docs/guide.md`، مثال‌ها (`examples/cpp/outlines.cpp`, `examples/python/12_vector_outlines.py`).
- [ ] بنچمارک rasterizer (سرعت fill/stroke نسبت به plutovg و Pillow/cairo) و بهینه‌سازی در صورت لزوم.
- [ ] بایندینگ پایتون برای `font.outline` تا `Outline(font.outline(g))` مستقیم کار کند.
- [ ] تأیید novasvg روی goldenهای خودش (باید با تلرانس 0 برابر باشد؛ اگر نبود اول تفاوت FMA/پرچم کامپایل را بررسی کن).

## Logging (stb::logging)
- [x] `stb/logging.hpp` + `src/util/logging.cpp`: `logger`، `handler` (`stream`، `file`، `callback`، `null`)، `formatter`، `level`، `get_logger`، `basic_config`، `LIBSTB_LOG`؛ دو هشدار stderr از `libstb.simd` و `libstb.threads` رد می‌شوند.
- [x] `tests/cpp/test_logging.cpp` (19 تست: سطح‌ها، سلسله‌مراتب، propagate، handlerها، formatter، چند thread)، `docs/guide.md`، `CHANGELOG.md`.
- [x] پل پایتون (`bind_logging.cpp`): `callback_handler` روی `libstb` که به `logging.getLogger(name).log(levelno, message)` می‌رود؛ با `import libstb` خودکار نصب می‌شود؛ تنها تابع عمومی `libstb.logging_bridge(enabled)`؛ `_log` خصوصی برای تست؛ `tests/python/test_logging.py`.
- [x] handlerهای OS log (`include/stb/logging_os.hpp`: `syslog`، `android`، `unified` (os_log)، `debug_string`، `event_log`) با ماکروهای `STB_LOG_HAS_*`،
      `src/util/logging_os.cpp`، تست `tests/cpp/test_logging_os.cpp`. **نوشته شد ولی اجرا نشده (CI اولین اجراست؛ Android بدون CI).** کد از aixlog کپی نشد،
      پس چیزی در `NOTICE.md` نرفت.
- [x] logging خود کتابخانه (`src/util/log.hpp` و فراخوانی‌ها)، `tests/cpp/test_library_logging.cpp`، `docs/adr/0001..0003`.
- [x] **پایتون: API فونت.** `Font.glyph_index`، `glyph_count`، `glyph_info`، `design_metrics`، `outline`، `glyph_kerning`،
      `Font.face_count` و `Font.shape` (با `direction`/`kerning`/`ligatures`)، در واحد فونت و y رو به بالا مثل C++؛
      `Outline(font.outline(g))` کار می‌کند. `shape` برای متن‌های 512 بایتی و بیشتر GIL را آزاد می‌کند. تست‌ها در
      `tests/python/test_font.py`؛ مستندات در `docs/guide.md` («From Python»).
- [x] **`data/` و مثال‌ها و تست‌هایی که از آن می‌خوانند.** چهار عکس، یک RGBA ساخته‌شده با libstb، فونت Vazirmatn (Regular و Bold)؛ لایسنس‌ها در
      `data/README.md`؛ همه‌ی مثال‌ها و نوت‌بوک‌ها روی آن‌ها؛ تست‌های `test_sample_data.py`، `test_sample_fonts.py`، `test_sample_data.cpp`.
- [x] **باگ leak هنگام خروج** (`np.from_dlpack(img)` سپس `Image.from_dlpack`) درست شد (`nb::is_alive()`) و تست دارد.
