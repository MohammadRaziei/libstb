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
      دو سرش می‌گذره (عرض فرد crisp می‌مونه، عرض زوج بین دو ردیف می‌افته). کد توی `src/core/shape.cpp`
      با `-ffp-contract=off` کامپایل می‌شه که روی ARM هم همون بیت‌ها دربیاد.
      بیضی با تخمین مرتبه‌ی اول فاصله کشیده می‌شه: روی محورها ضخامت حاشیه دقیقه (اختلاف تا ۰.۰۴
      پیکسل اندازه‌گیری شد)، بین محورها اندازه‌گیری نشده.
      دو golden hash (متن، و شکل+متن) بین تست C++ و Python مشترکن؛ ثابت‌هاشون روی ویندوز/macOS هنوز
      تأیید نشده (متن با stb_truetype و کامپایلرهای دیگه ممکنه آخرین بیت رو عوض کنه).
- [x] **`text_align` (left, center, right)** برای `draw_text` و `canvas` (`align=` توی Python). با center/right،
      x مرکز یا لبه‌ی راستِ هر خط می‌شه. `measure` از قبل وجود داشت (`font::measure`, `Font.measure`).
- [x] README (خط ۳) و توضیح PyPI (`pyproject.toml`) حالا zlib و نوشتن متن رو هم می‌گن.
- [x] مستندات: بیلد `stb_docs` با صفر هشدار (هشدارهای Doxygen توی `color.hpp` و docstring اسفینکس درست شد)؛
      رنگ prompt (`>>>`) توی بلوک doctest دارک اسفینکس خوانا شد.
- [x] README، `docs/guide.md`، `docs/cpp/mainpage.md`، `docs/python/source/font.rst` و
      `examples/python/05_font_render.py` آپدیت شدن.
- [x] تست: ۱۸۰ تست C++ پاس. Python: ۱۱۷۶ پاس، ۲ skip (`pytest_memray` و `torch` نصب نبودن).
- [x] کارهای بالا (به‌جز `align` و مستندات اخیر) توی `053fe98` روی `origin/master` هستن؛ بقیه کامیت‌های محلی با
      اسم placeholder `Claude` هستن (اگه خواستی `git commit --amend --reset-author`).

## قبل از هر چیز دیگه

- [ ] **push و چک CI.** `git push origin master`؛ اگه چیزی push شده بود قبلش `git pull --rebase`.
      ببین job `build-docs` سبز می‌شه و `Canvas`/`Image.draw_text` توی Sphinx و Doxygen درست دیده
      می‌شن (سایت رو من بیلد نکردم). سایت فقط با push به `master` ساخته می‌شه (`orchestrator.yml`).
- [ ] **ویندوز/macOS/Clang تست نشده.** همه‌چی فقط روی لینوکس/GCC اجرا شد. `color.hpp` از
      `std::clamp` استفاده می‌کنه (ماکروهای `<windows.h>` فقط `min`/`max` رو می‌گیرن)، ولی تایید با CI میاد.

## قابلیت‌ها

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
- [ ] نسخه‌ی بعدی (پیشنهاد: 0.8.0) و ورودی `CHANGELOG.md` برای `draw_text`، `color` و `canvas`.
- [ ] اگه خواستی، اسم‌های داخلی `LIBSTB_*` یکدست بشن (فقط تغییر اسمه).

## دام‌ها

- `composite_in_place` روی تصویری که حافظه‌ی بیرونی رو wrap کرده (مثلاً numpy) همون حافظه رو تغییر
  می‌ده. `canvas` همیشه روی یه کپی کار می‌کنه، پس امنه.
- `draw_text` فونت ناشناس رو لود نکن: stb_truetype بررسی مرز نداره (فقط فونت مورد اعتماد).
- گزارش بنچمارک تک‌فایله‌ست و بعد از هر تغییر قالب باید دوباره ساخته بشه (دستورش توی هندآف قبلی).
