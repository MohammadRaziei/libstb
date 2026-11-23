# binding.md: چطور بایندینگ Python نوع ورودی را انتخاب می‌کند

تاریخ: 2026-11-21 (روی کامیت `8fc443b`). این فایل یادداشت طراحی است، نه مستند کاربر: چه مشکلی دیدیم، چه چیزی را اندازه گرفتیم، چه چیزی را تصمیم گرفتیم و چه چیزی باز است.
وضعیت (2026-11-23): **مرحله‌ی ۱الف پیاده شد** (بخش ۷). ۱ب و ۲ باز هستند.

## ۱. مسئله

C++ با قالب (`mat<T>`، `image8u`، `mat32f`) کار می‌کند و nanobind هر نوع را overload جدا می‌بیند. کاربر Python یک اسم می‌خواهد که روی هر نوع ورودیِ معنی‌دار کار کند.

ورودی‌های معنی‌دار (هر چیز دیگر خطاست):

| ورودی | معنی | خروجی |
|---|---|---|
| `Image` | uint8، پیکسل ۰ تا ۲۵۵، با `pixel_format` | `Image` |
| آرایه‌ی numpy `uint8` شکل `(H, W[, C])` | همان uint8 | آرایه‌ی uint8 |
| آرایه‌ی numpy `float32` شکل `(H, W[, C])` | float، پیکسل ۰ تا ۱ | آرایه‌ی float32 |

قاعده: نوع خروجی همان نوع ورودی است.

## ۲. باگی که پیدا شد (روی `8fc443b`)

توابع با `f32_array` (۴۰ جا در `bind_filter.cpp`، دست‌کم ۲۷ تابع) و بدون `noconvert` بایند شده‌اند. آرایه‌ی uint8 به float32 **بی‌صدا تبدیل می‌شود ولی عددها همان ۰ تا ۲۵۵ می‌مانند**، در حالی که float یعنی ۰ تا ۱. نتیجه‌ی واقعی:

```python
stb.equalize(u8_array)      # float32، همه‌ی مقدارها 1.0 (غلط، بدون هشدار)
stb.gamma(u8_array, 2.0)    # همین‌طور
```

float64 و int هم به همین شکل بی‌صدا تبدیل می‌شوند. نسخه‌ی درجا هم فقط روی `Image` و با `inplace=True` است و روی آرایه وجود ندارد.

## ۳. آزمایش (nanobind، با CMake)

ماژول کوچکی با سه overload (`Img`، آرایه‌ی uint8، آرایه‌ی float32) ساختم و ورودی‌های مختلف دادم.

| ورودی | با `.noconvert()` | بدون `noconvert` (مثل امروز) |
|---|---|---|
| uint8 | نسخه‌ی uint8 | نسخه‌ی uint8 |
| float32 | نسخه‌ی float32 | نسخه‌ی float32 |
| float64 / int32 | `TypeError` | بی‌صدا به **اولین** overload می‌رود (ترتیب تعیین می‌کند) |
| کلاس `Img` | نسخه‌ی `Img` | نسخه‌ی `Img` |
| غیرپیوسته | `TypeError` | |
| read-only، با پارامتر غیر-`const` | `TypeError` (چیزی که `*_inplace` می‌خواهد) | |

نتیجه‌ها:

- nanobind خودش بر پایه‌ی dtype دسته‌بندی می‌کند، در C++ و سریع. فقط باید `.noconvert()` بگذاریم.
- پارامتر غیر-`const` همراه `noconvert` آرایه‌ی read-only و غیرپیوسته را رد می‌کند: کپی بی‌صدا نداریم و آرایه‌ی کاربر واقعاً تغییر می‌کند.
- پیام خطا همه‌ی امضاها را فهرست می‌کند (قابل‌قبول).
- stub که `python -m nanobind.stubgen` می‌سازد برای هر overload یک `@overload` با `NDArray[numpy.uint8]` یا `NDArray[numpy.float32]` دارد، یعنی جدول دسته‌بندی کامل در stub هست.

کد آزمایش (کامپایل با CMake و `nanobind_add_module`):

```cpp
using u8_ro  = nb::ndarray<const std::uint8_t, nb::c_contig, nb::device::cpu>;
using f32_ro = nb::ndarray<const float,        nb::c_contig, nb::device::cpu>;
using u8_rw  = nb::ndarray<std::uint8_t,       nb::c_contig, nb::device::cpu>;   // نیاز به آرایه‌ی writable
using f32_rw = nb::ndarray<float,              nb::c_contig, nb::device::cpu>;

m.def("f", [](Img&)   { return "Img"; }, "x"_a);
m.def("f", [](u8_ro)  { return "u8";  }, "x"_a.noconvert());
m.def("f", [](f32_ro) { return "f32"; }, "x"_a.noconvert());
m.def("h", [](u8_rw)  { return "u8 rw";  }, "x"_a.noconvert());
m.def("h", [](f32_rw) { return "f32 rw"; }, "x"_a.noconvert());
```

## ۴. تصمیم‌ها

1. **دسته‌بندی توابع بومی است:** overloadهای nanobind، هر آرگومان آرایه با `.noconvert()`. برای توابع هیچ لایه‌ی پایتونی تولیدشده‌ای نمی‌گذاریم (هر فراخوانی یک لایه‌ی اضافه نمی‌خرد).
2. **یک کمک‌تابع C++ header-only** سه overload (`Image`، آرایه‌ی uint8، آرایه‌ی float32) را از **یک lambda عمومی** (`auto&`) ثبت می‌کند، به‌جای نوشتن دستی شانزده یا چهل بار. اسم و امضایش طبق قاعده‌ی «اول API را شرح بده» قبل از پیاده‌سازی پیشنهاد و تأیید می‌شود. جای آینده‌اش می‌تواند `polybind` باشد، برای پروژه‌های دیگر.
3. **`*_inplace` در Python تابع جداست، نه `inplace=True`:** اسم‌ها دقیقاً مثل C++ (`equalize_inplace`، `gamma_inplace`، ...)، هم تابع آزاد روی `Image` و آرایه، هم متد `Image`. شانزده تابع: `convert_color`، `pixelize`، `unsharp_mask`، هفت تای morphology (`erode`، `dilate`، `morph_open`، `morph_close`، `morph_gradient`، `top_hat`، `black_hat`) و شش تای tone (`equalize`، `auto_contrast`، `match_histogram`، `clahe`، `gamma`، `levels`).
4. **زنجیره‌ای در هر دو زبان:** تابع Python شیء ورودی (خودِ `Image` یا خودِ آرایه) را برمی‌گرداند و متد `self` را. در C++ هر `*_inplace` به‌جای `void`، **خودِ آرگومان را با ارجاع** برمی‌گرداند. چون `image8u` از `mat8u` ارث می‌برد، برای حفظ نوع دقیق (`image&`) همان الگوی `canny` و `threshold` لازم است: کار اصلی در `detail::..._inplace_mat<T>` و یک پوشش نازک `template <class M> M& equalize_inplace(M&, ...)`.
5. **آرایه برای درجا باید writable و C-contiguous باشد،** وگرنه `TypeError` (از nanobind، با `noconvert`). هیچ‌وقت کپی بی‌صدا نمی‌گیریم.
6. **آرایه‌ی uint8 مستقیم پذیرفته می‌شود،** و float64 و int خطا می‌دهند، نه تبدیل بی‌صدا. این قاعده برای همه‌ی توابع است، نه فقط درجا.
7. **`inplace=` حذف می‌شود،** بدون alias (سازگاری برای مالک مهم نیست، فقط خودش کاربر است).

نکته‌ی مرتبط: `Image(arr)` وقتی آرایه writable و پیوسته باشد روی همان حافظه می‌نشیند (بدون کپی)، وگرنه **بی‌صدا کپی می‌گیرد**. این را در مستندات `Image(array)` بنویسیم.

## ۵. ترتیب کار

- **۱الف:** شانزده تابع `*_inplace` با سه ورودی، زنجیره‌ای، و حذف `inplace=` (طبق ۲ تا ۷).
- **۱ب:** همین قاعده (سه ورودی و `noconvert`) روی توابع موجود، که باگ بخش ۲ را می‌بندد.
- **۲:** تصمیم درباره‌ی کلاس `Mat` و گسترش `polybind` (پایین).
- باز: ۱الف و ۱ب در یک patch باشند یا دو patch؟ هنوز تصمیم نگرفته‌ایم.

## ۶. باز: کلاس `Mat` و `polybind`

`libstb` برای داده‌ی float در Python کلاسی ندارد، فقط آرایه‌ی numpy. روی آرایه متد زنجیره‌ای نمی‌شود (فقط تو در تو: `gamma_inplace(equalize_inplace(arr), 2.2)`). اگر یک کلاس `Mat` برای داده‌ی خام (uint8 و float32) اضافه شود:

- روی float هم متد زنجیره‌ای داریم و `isinstance(x, Mat)` برای همه‌ی نوع‌ها درست است،
- `libstb` اولین کاربر واقعی `polybind wrap` می‌شود،
- هزینه: سطح API بزرگ‌تر و نگه‌داری بیشتر.

وضعیت `polybind` (مخزن `mohammadraziei/polybind`، خوانده‌ام، اجرا نکرده‌ام):

- `polybind wrap` از `.pyi` فقط **کلاس**‌هایی با اسم `Base__T1__T2` را به یک کلاس `Base` تبدیل می‌کند. توابع آزاد را پوشش نمی‌دهد.
- نسخه را با `type(a).__name__` (نام نوع پایتونی: `int`، `float`، `str`) انتخاب می‌کند. آرایه‌ی uint8 و float32 هر دو `ndarray` هستند، پس تفکیکشان ممکن نیست؛ کلاس ورودی (مثل `Image`) را هم نمی‌بیند.
- برای این کار لازم است: تشخیص با `ndarray.dtype` و با کلاس ورودی، و ساختن جدول از حاشیه‌نویسی‌های stub (`NDArray[numpy.float32]`) به‌جای قرارداد اسم‌گذاری.
- اگر تصمیم درباره‌ی `Mat` مثبت شود، گسترش `polybind` با مثال واقعی `libstb` انجام می‌شود.

## ۷. مرحله‌ی ۱الف: آنچه پیاده شد (2026-11-23)

- **C++:** شانزده `*_inplace` خودِ آرگومان را با نوع دقیق برمی‌گردانند (`traits::if_image_mutable_t<M>`)؛ کار اصلی در `detail::..._inplace_mat<T>`؛ `edge_mode`، `tone_mode` و `color_space` تابع `*_name` و `*_from_name` عمومی دارند.
- **Python:** هسته‌ی دسته‌بندی به `polybind` رفت (مخزن `mohammadraziei/polybind`: `polybind/dispatch.hpp`، `nanobind.hpp`، target `polybind::polybind`)، و اینجا یک کپی با `polybind vendor src/bindings/python` است (`src/bindings/python/polybind/`، stamp `VENDORED`). `stb_kinds.hpp` سه نوع (`stb::image`، `stb::mat8u`، `stb::mat32f`) و سه enum را معرفی می‌کند.
- **تفاوت با طرح اولیه:** یک تابع `def_dispatch` (بدون برچسب؛ درجا بودن از `auto&` خوانده می‌شود)؛ سخت‌گیری با نوع پارامتر `polybind::strict<...>` است، نه `.noconvert()` روی توصیف‌گر (در nanobind 3 مقدار برگشتی `noconvert()` نوع تازه است و پارامتر دوم توصیف‌گر ندارد)؛ lint یک `RuntimeWarning` هنگام import است (`POLYBIND_LINT`)؛ گزینه‌ی `-DPOLYBIND_LINT=OFF` آن را خاموش می‌کند.
- **رفتار:** `inplace=True` حذف شد. آرایه‌ی float64 و int32 و read-only و غیرپیوسته `TypeError` است. `Image` به‌عنوان پارامتر **دوم** (مرجع) هنوز یک آرایه‌ی uint8 را با تبدیل ضمنیِ خودِ `Image` می‌پذیرد (طراحی قبلی کتابخانه).
- **تست:** `tests/python/test_inplace.py` (هر شانزده تابع روی سه نوع، زنجیره، خطاها، enum، شکل آرایه). ساخت با CMake بدون warning؛ C++ 753 و Python 1941 پاس.
- **هنوز باز (۱ب):** همین قاعده (سه ورودی و `strict`) روی ۲۷ تابع غیردرجا (`equalize`، `gamma`، ...) که باگ تبدیل بی‌صدا را (آرایه‌ی uint8 به float32 با مقدارهای ۰ تا ۲۵۵) هنوز دارند؛ پشتیبانی `def_dispatch` از تابعی که شیء تازه می‌سازد (پارامتر اول `const auto&`)؛ MSVC در CI؛ اندازه‌گیری هزینه‌ی خود لایه روی `libstb` (فقط روی ماژول آزمایشی nanobind اندازه گرفته شد).
