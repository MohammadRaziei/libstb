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
- [x] **`canvas` (C++) و `Canvas` (Python)**: چند متن روی یک کپی از تصویر (`Image.canvas()`).
      `image::composite_in_place` و `font::draw_text_in_place` اضافه شدن؛ `composite()` و
      `draw_text()` الان کپی + نسخه‌ی درجا هستن.
- [x] README، `docs/guide.md`، `docs/cpp/mainpage.md`، `docs/python/source/font.rst` و
      `examples/python/05_font_render.py` آپدیت شدن.
- [x] تست: ۱۷۷ تست C++ پاس. Python قبل از rebase: ۱۱۷۳ پاس، ۲ skip (`pytest_memray` و `torch` نصب نبودن).
- [x] چهار کامیت روی `054ea1f` (rebase شد، بدون conflict): `draw_text`، `color`، rename به snake_case،
      `canvas`. با اسم placeholder `Claude` (اگه خواستی `git commit --amend --reset-author`).

## قبل از هر چیز دیگه

- [ ] **push و چک CI.** `git push origin master`؛ اگه چیزی push شده بود قبلش `git pull --rebase`.
      ببین job `build-docs` سبز می‌شه و `Canvas`/`Image.draw_text` توی Sphinx و Doxygen درست دیده
      می‌شن (سایت رو من بیلد نکردم). سایت فقط با push به `master` ساخته می‌شه (`orchestrator.yml`).
- [ ] **تست‌های Python بعد از rebase دوباره اجرا نشدن** (فقط C++ اجرا شد). روی CI ببین.
- [ ] **ویندوز/macOS/Clang تست نشده.** همه‌چی فقط روی لینوکس/GCC اجرا شد. `color.hpp` از
      `std::clamp` استفاده می‌کنه (ماکروهای `<windows.h>` فقط `min`/`max` رو می‌گیرن)، ولی تایید با CI میاد.

## قابلیت‌ها

- [ ] **`measure` و تراز متن.** الان فقط (x, y) گوشه‌ی بالا-چپ رو می‌گیریم. برای وسط‌چین باید عرض متن
      رو خودت حساب کنی. اولویت پیشنهادی: `align` (چپ/وسط/راست) یا حداقل اندازه‌گیریِ متن.
- [ ] اگه گزینه‌ها زیاد شدن (تراز، فاصله‌ی خطوط، حداکثر عرض با شکستن خط، سایه، outline): یه struct گزینه
      با مقدار پیش‌فرض (`{.color = ..., .align = ...}` توی C++ و kwargs توی Python)، نه بیلدر.
- [ ] `color` توی Python فقط رشته/عدد/tuple قبول می‌کنه و کلاس مستقل نیست. اگه لازم شد expose بشه.
- [ ] هزینه‌ی `canvas` در برابر چند بار `draw_text` **اندازه‌گیری نشده**. فقط ثابت‌بودنِ اشاره‌گر داده
      تست شده (یعنی کپی اضافه نیست). تا عدد نداریم ادعای سرعت ننویس.
- [ ] تصمیم: دو نسخه‌ی `Color` (novasvg با CamelCase، libstb با snake_case). ببین `checklist.md`
      توی novasvg (بخش «Color مشترک با libstb»).

## سایت و مستندات

- [ ] **دارک تم یکدست:** هیرو landing، Sphinx و Doxygen توی دارک سبز برند بشن (مثل گزارش بنچمارک)؛
      بعد Sphinx و Doxygen با توکن‌های landing یکی بشن (تم تاریک، سطح کد ملایم، بدون خط تزئینی)؛
      بعد `tests/genhtml.css` (پالت قدیمی).
- [ ] **متن‌های «فقط تصویر»:** `pyproject.toml` خط ۸ (توضیحِ صفحه‌ی PyPI) و `README.md` خط ۳ باید
      zlib و نوشتن متن رو هم بگن؛ `docs/guide.md` و `docs/python/source/index.rst` رو هم بررسی کن.
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
