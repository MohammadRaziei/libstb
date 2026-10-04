/* stb_image_write - v1.16 - public domain - http://nothings.org/stb
   writes out PNG/BMP/TGA/JPEG/HDR images to C stdio - Sean Barrett 2010-2015
                                     no warranty implied; use at your own risk

   Before #including,

       #define STB_IMAGE_WRITE_IMPLEMENTATION

   in the file that you want to have the implementation.

   Will probably not work correctly with strict-aliasing optimizations.

ABOUT:

   This header file is a library for writing images to C stdio or a callback.

   The PNG output is not optimal; it is 20-50% larger than the file
   written by a decent optimizing implementation; though providing a custom
   zlib compress function (see STBIW_ZLIB_COMPRESS) can mitigate that.
   This library is designed for source code compactness and simplicity,
   not optimal image file size or run-time performance.

BUILDING:

   You can #define STBIW_ASSERT(x) before the #include to avoid using assert.h.
   You can #define STBIW_MALLOC(), STBIW_REALLOC(), and STBIW_FREE() to replace
   malloc,realloc,free.
   You can #define STBIW_MEMMOVE() to replace memmove()
   You can #define STBIW_ZLIB_COMPRESS to use a custom zlib-style compress function
   for PNG compression (instead of the builtin one), it must have the following signature:
   unsigned char * my_compress(unsigned char *data, int data_len, int *out_len, int quality);
   The returned data will be freed with STBIW_FREE() (free() by default),
   so it must be heap allocated with STBIW_MALLOC() (malloc() by default),

UNICODE:

   If compiling for Windows and you wish to use Unicode filenames, compile
   with
       #define STBIW_WINDOWS_UTF8
   and pass utf8-encoded filenames. Call stbiw_convert_wchar_to_utf8 to convert
   Windows wchar_t filenames to utf8.

USAGE:

   There are five functions, one for each image file format:

     int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
     int stbi_write_bmp(char const *filename, int w, int h, int comp, const void *data);
     int stbi_write_tga(char const *filename, int w, int h, int comp, const void *data);
     int stbi_write_jpg(char const *filename, int w, int h, int comp, const void *data, int quality);
     int stbi_write_hdr(char const *filename, int w, int h, int comp, const float *data);

     void stbi_flip_vertically_on_write(int flag); // flag is non-zero to flip data vertically

   There are also five equivalent functions that use an arbitrary write function. You are
   expected to open/close your file-equivalent before and after calling these:

     int stbi_write_png_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data, int stride_in_bytes);
     int stbi_write_bmp_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
     int stbi_write_tga_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
     int stbi_write_hdr_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const float *data);
     int stbi_write_jpg_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data, int quality);

   where the callback is:
      void stbi_write_func(void *context, void *data, int size);

   You can configure it with these global variables:
      int stbi_write_tga_with_rle;             // defaults to true; set to 0 to disable RLE
      int stbi_write_png_compression_level;    // defaults to 8; set to higher for more compression
      int stbi_write_force_png_filter;         // defaults to -1; set to 0..5 to force a filter mode


   You can define STBI_WRITE_NO_STDIO to disable the file variant of these
   functions, so the library will not use stdio.h at all. However, this will
   also disable HDR writing, because it requires stdio for formatted output.

   Each function returns 0 on failure and non-0 on success.

   The functions create an image file defined by the parameters. The image
   is a rectangle of pixels stored from left-to-right, top-to-bottom.
   Each pixel contains 'comp' channels of data stored interleaved with 8-bits
   per channel, in the following order: 1=Y, 2=YA, 3=RGB, 4=RGBA. (Y is
   monochrome color.) The rectangle is 'w' pixels wide and 'h' pixels tall.
   The *data pointer points to the first byte of the top-left-most pixel.
   For PNG, "stride_in_bytes" is the distance in bytes from the first byte of
   a row of pixels to the first byte of the next row of pixels.

   PNG creates output files with the same number of components as the input.
   The BMP format expands Y to RGB in the file format and does not
   output alpha.

   PNG supports writing rectangles of data even when the bytes storing rows of
   data are not consecutive in memory (e.g. sub-rectangles of a larger image),
   by supplying the stride between the beginning of adjacent rows. The other
   formats do not. (Thus you cannot write a native-format BMP through the BMP
   writer, both because it is in BGR order and because it may have padding
   at the end of the line.)

   PNG allows you to set the deflate compression level by setting the global
   variable 'stbi_write_png_compression_level' (it defaults to 8).

   HDR expects linear float data. Since the format is always 32-bit rgb(e)
   data, alpha (if provided) is discarded, and for monochrome data it is
   replicated across all three channels.

   TGA supports RLE or non-RLE compressed data. To use non-RLE-compressed
   data, set the global variable 'stbi_write_tga_with_rle' to 0.

   JPEG does ignore alpha channels in input data; quality is between 1 and 100.
   Higher quality looks better but results in a bigger image.
   JPEG baseline (no JPEG progressive).

CREDITS:


   Sean Barrett           -    PNG/BMP/TGA
   Baldur Karlsson        -    HDR
   Jean-Sebastien Guay    -    TGA monochrome
   Tim Kelsey             -    misc enhancements
   Alan Hickman           -    TGA RLE
   Emmanuel Julien        -    initial file IO callback implementation
   Jon Olick              -    original jo_jpeg.cpp code
   Daniel Gibson          -    integrate JPEG, allow external zlib
   Aarni Koskela          -    allow choosing PNG filter

   bugfixes:
      github:Chribba
      Guillaume Chereau
      github:jry2
      github:romigrou
      Sergio Gonzalez
      Jonas Karlsson
      Filip Wasil
      Thatcher Ulrich
      github:poppolopoppo
      Patrick Boettcher
      github:xeekworx
      Cap Petschulat
      Simon Rodriguez
      Ivan Tikhonov
      github:ignotion
      Adam Schackart
      Andrew Kensler

LICENSE

  See end of file for license information.

*/
/* LIBSTB LOCAL PATCHES: this vendored copy differs from upstream v1.16 in two places, each
   marked "libstb patch" in the code.
   1. The built-in zlib compressor (stbi_zlib_compress, used for PNG) was rewritten:
      4-byte hash-chain LZ77 with lazy matching and dynamic Huffman blocks (chosen per
      block against fixed / stored). Its output is an ordinary zlib stream, typically
      within ~1% of zlib level 6 in size. The STBIW_ZLIB_COMPRESS hook and the
      stbi_zlib_compress signature are unchanged.
   2. The JPEG encoder is faster (about 1.2x to 1.5x in the benchmark suite) and writes exactly the bytes upstream
      does: a buffered 64-bit entropy bit writer (upstream called the sink once per output
      byte), an AC loop that walks only the non-zero coefficients, a vectorizable DCT and
      quantizer, and SSE2 colour conversion / transposes on x86 (plain C elsewhere).
      tests/python/test_jpeg_golden.py pins the output bytes.
   See src/third_party/README.md. */


#ifndef INCLUDE_STB_IMAGE_WRITE_H
#define INCLUDE_STB_IMAGE_WRITE_H

#include <stdlib.h>

// if STB_IMAGE_WRITE_STATIC causes problems, try defining STBIWDEF to 'inline' or 'static inline'
#ifndef STBIWDEF
#ifdef STB_IMAGE_WRITE_STATIC
#define STBIWDEF  static
#else
#ifdef __cplusplus
#define STBIWDEF  extern "C"
#else
#define STBIWDEF  extern
#endif
#endif
#endif

#ifndef STB_IMAGE_WRITE_STATIC  // C++ forbids static forward declarations
STBIWDEF int stbi_write_tga_with_rle;
STBIWDEF int stbi_write_png_compression_level;
STBIWDEF int stbi_write_force_png_filter;
#endif

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_png(char const *filename, int w, int h, int comp, const void  *data, int stride_in_bytes);
STBIWDEF int stbi_write_bmp(char const *filename, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_tga(char const *filename, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_hdr(char const *filename, int w, int h, int comp, const float *data);
STBIWDEF int stbi_write_jpg(char const *filename, int x, int y, int comp, const void  *data, int quality);

#ifdef STBIW_WINDOWS_UTF8
STBIWDEF int stbiw_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input);
#endif
#endif

typedef void stbi_write_func(void *context, void *data, int size);

STBIWDEF int stbi_write_png_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data, int stride_in_bytes);
STBIWDEF int stbi_write_bmp_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_tga_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_hdr_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const float *data);
STBIWDEF int stbi_write_jpg_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void  *data, int quality);

STBIWDEF void stbi_flip_vertically_on_write(int flip_boolean);

#endif//INCLUDE_STB_IMAGE_WRITE_H

#ifdef STB_IMAGE_WRITE_IMPLEMENTATION

#ifdef _WIN32
   #ifndef _CRT_SECURE_NO_WARNINGS
   #define _CRT_SECURE_NO_WARNINGS
   #endif
   #ifndef _CRT_NONSTDC_NO_DEPRECATE
   #define _CRT_NONSTDC_NO_DEPRECATE
   #endif
#endif

#ifndef STBI_WRITE_NO_STDIO
#include <stdio.h>
#endif // STBI_WRITE_NO_STDIO

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(STBIW_MALLOC) && defined(STBIW_FREE) && (defined(STBIW_REALLOC) || defined(STBIW_REALLOC_SIZED))
// ok
#elif !defined(STBIW_MALLOC) && !defined(STBIW_FREE) && !defined(STBIW_REALLOC) && !defined(STBIW_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBIW_MALLOC, STBIW_FREE, and STBIW_REALLOC (or STBIW_REALLOC_SIZED)."
#endif

#ifndef STBIW_MALLOC
#define STBIW_MALLOC(sz)        malloc(sz)
#define STBIW_REALLOC(p,newsz)  realloc(p,newsz)
#define STBIW_FREE(p)           free(p)
#endif

#ifndef STBIW_REALLOC_SIZED
#define STBIW_REALLOC_SIZED(p,oldsz,newsz) STBIW_REALLOC(p,newsz)
#endif


#ifndef STBIW_MEMMOVE
#define STBIW_MEMMOVE(a,b,sz) memmove(a,b,sz)
#endif


#ifndef STBIW_ASSERT
#include <assert.h>
#define STBIW_ASSERT(x) assert(x)
#endif

#define STBIW_UCHAR(x) (unsigned char) ((x) & 0xff)

#ifdef STB_IMAGE_WRITE_STATIC
static int stbi_write_png_compression_level = 8;
static int stbi_write_tga_with_rle = 1;
static int stbi_write_force_png_filter = -1;
#else
int stbi_write_png_compression_level = 8;
int stbi_write_tga_with_rle = 1;
int stbi_write_force_png_filter = -1;
#endif

static int stbi__flip_vertically_on_write = 0;

STBIWDEF void stbi_flip_vertically_on_write(int flag)
{
   stbi__flip_vertically_on_write = flag;
}

typedef struct
{
   stbi_write_func *func;
   void *context;
   unsigned char buffer[64];
   int buf_used;
} stbi__write_context;

// initialize a callback-based context
static void stbi__start_write_callbacks(stbi__write_context *s, stbi_write_func *c, void *context)
{
   s->func    = c;
   s->context = context;
}

#ifndef STBI_WRITE_NO_STDIO

static void stbi__stdio_write(void *context, void *data, int size)
{
   fwrite(data,1,size,(FILE*) context);
}

#if defined(_WIN32) && defined(STBIW_WINDOWS_UTF8)
#ifdef __cplusplus
#define STBIW_EXTERN extern "C"
#else
#define STBIW_EXTERN extern
#endif
STBIW_EXTERN __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *str, int cbmb, wchar_t *widestr, int cchwide);
STBIW_EXTERN __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t *widestr, int cchwide, char *str, int cbmb, const char *defchar, int *used_default);

STBIWDEF int stbiw_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input)
{
   return WideCharToMultiByte(65001 /* UTF8 */, 0, input, -1, buffer, (int) bufferlen, NULL, NULL);
}
#endif

static FILE *stbiw__fopen(char const *filename, char const *mode)
{
   FILE *f;
#if defined(_WIN32) && defined(STBIW_WINDOWS_UTF8)
   wchar_t wMode[64];
   wchar_t wFilename[1024];
   if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, filename, -1, wFilename, sizeof(wFilename)/sizeof(*wFilename)))
      return 0;

   if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))
      return 0;

#if defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != _wfopen_s(&f, wFilename, wMode))
      f = 0;
#else
   f = _wfopen(wFilename, wMode);
#endif

#elif defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != fopen_s(&f, filename, mode))
      f=0;
#else
   f = fopen(filename, mode);
#endif
   return f;
}

static int stbi__start_write_file(stbi__write_context *s, const char *filename)
{
   FILE *f = stbiw__fopen(filename, "wb");
   stbi__start_write_callbacks(s, stbi__stdio_write, (void *) f);
   return f != NULL;
}

static void stbi__end_write_file(stbi__write_context *s)
{
   fclose((FILE *)s->context);
}

#endif // !STBI_WRITE_NO_STDIO

typedef unsigned int stbiw_uint32;
typedef int stb_image_write_test[sizeof(stbiw_uint32)==4 ? 1 : -1];

static void stbiw__writefv(stbi__write_context *s, const char *fmt, va_list v)
{
   while (*fmt) {
      switch (*fmt++) {
         case ' ': break;
         case '1': { unsigned char x = STBIW_UCHAR(va_arg(v, int));
                     s->func(s->context,&x,1);
                     break; }
         case '2': { int x = va_arg(v,int);
                     unsigned char b[2];
                     b[0] = STBIW_UCHAR(x);
                     b[1] = STBIW_UCHAR(x>>8);
                     s->func(s->context,b,2);
                     break; }
         case '4': { stbiw_uint32 x = va_arg(v,int);
                     unsigned char b[4];
                     b[0]=STBIW_UCHAR(x);
                     b[1]=STBIW_UCHAR(x>>8);
                     b[2]=STBIW_UCHAR(x>>16);
                     b[3]=STBIW_UCHAR(x>>24);
                     s->func(s->context,b,4);
                     break; }
         default:
            STBIW_ASSERT(0);
            return;
      }
   }
}

static void stbiw__writef(stbi__write_context *s, const char *fmt, ...)
{
   va_list v;
   va_start(v, fmt);
   stbiw__writefv(s, fmt, v);
   va_end(v);
}

static void stbiw__write_flush(stbi__write_context *s)
{
   if (s->buf_used) {
      s->func(s->context, &s->buffer, s->buf_used);
      s->buf_used = 0;
   }
}

static void stbiw__putc(stbi__write_context *s, unsigned char c)
{
   s->func(s->context, &c, 1);
}

static void stbiw__write1(stbi__write_context *s, unsigned char a)
{
   if ((size_t)s->buf_used + 1 > sizeof(s->buffer))
      stbiw__write_flush(s);
   s->buffer[s->buf_used++] = a;
}

static void stbiw__write3(stbi__write_context *s, unsigned char a, unsigned char b, unsigned char c)
{
   int n;
   if ((size_t)s->buf_used + 3 > sizeof(s->buffer))
      stbiw__write_flush(s);
   n = s->buf_used;
   s->buf_used = n+3;
   s->buffer[n+0] = a;
   s->buffer[n+1] = b;
   s->buffer[n+2] = c;
}

static void stbiw__write_pixel(stbi__write_context *s, int rgb_dir, int comp, int write_alpha, int expand_mono, unsigned char *d)
{
   unsigned char bg[3] = { 255, 0, 255}, px[3];
   int k;

   if (write_alpha < 0)
      stbiw__write1(s, d[comp - 1]);

   switch (comp) {
      case 2: // 2 pixels = mono + alpha, alpha is written separately, so same as 1-channel case
      case 1:
         if (expand_mono)
            stbiw__write3(s, d[0], d[0], d[0]); // monochrome bmp
         else
            stbiw__write1(s, d[0]);  // monochrome TGA
         break;
      case 4:
         if (!write_alpha) {
            // composite against pink background
            for (k = 0; k < 3; ++k)
               px[k] = bg[k] + ((d[k] - bg[k]) * d[3]) / 255;
            stbiw__write3(s, px[1 - rgb_dir], px[1], px[1 + rgb_dir]);
            break;
         }
         /* FALLTHROUGH */
      case 3:
         stbiw__write3(s, d[1 - rgb_dir], d[1], d[1 + rgb_dir]);
         break;
   }
   if (write_alpha > 0)
      stbiw__write1(s, d[comp - 1]);
}

static void stbiw__write_pixels(stbi__write_context *s, int rgb_dir, int vdir, int x, int y, int comp, void *data, int write_alpha, int scanline_pad, int expand_mono)
{
   stbiw_uint32 zero = 0;
   int i,j, j_end;

   if (y <= 0)
      return;

   if (stbi__flip_vertically_on_write)
      vdir *= -1;

   if (vdir < 0) {
      j_end = -1; j = y-1;
   } else {
      j_end =  y; j = 0;
   }

   for (; j != j_end; j += vdir) {
      for (i=0; i < x; ++i) {
         unsigned char *d = (unsigned char *) data + (j*x+i)*comp;
         stbiw__write_pixel(s, rgb_dir, comp, write_alpha, expand_mono, d);
      }
      stbiw__write_flush(s);
      s->func(s->context, &zero, scanline_pad);
   }
}

static int stbiw__outfile(stbi__write_context *s, int rgb_dir, int vdir, int x, int y, int comp, int expand_mono, void *data, int alpha, int pad, const char *fmt, ...)
{
   if (y < 0 || x < 0) {
      return 0;
   } else {
      va_list v;
      va_start(v, fmt);
      stbiw__writefv(s, fmt, v);
      va_end(v);
      stbiw__write_pixels(s,rgb_dir,vdir,x,y,comp,data,alpha,pad, expand_mono);
      return 1;
   }
}

static int stbi_write_bmp_core(stbi__write_context *s, int x, int y, int comp, const void *data)
{
   if (comp != 4) {
      // write RGB bitmap
      int pad = (-x*3) & 3;
      return stbiw__outfile(s,-1,-1,x,y,comp,1,(void *) data,0,pad,
              "11 4 22 4" "4 44 22 444444",
              'B', 'M', 14+40+(x*3+pad)*y, 0,0, 14+40,  // file header
               40, x,y, 1,24, 0,0,0,0,0,0);             // bitmap header
   } else {
      // RGBA bitmaps need a v4 header
      // use BI_BITFIELDS mode with 32bpp and alpha mask
      // (straight BI_RGB with alpha mask doesn't work in most readers)
      return stbiw__outfile(s,-1,-1,x,y,comp,1,(void *)data,1,0,
         "11 4 22 4" "4 44 22 444444 4444 4 444 444 444 444",
         'B', 'M', 14+108+x*y*4, 0, 0, 14+108, // file header
         108, x,y, 1,32, 3,0,0,0,0,0, 0xff0000,0xff00,0xff,0xff000000u, 0, 0,0,0, 0,0,0, 0,0,0, 0,0,0); // bitmap V4 header
   }
}

STBIWDEF int stbi_write_bmp_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_bmp_core(&s, x, y, comp, data);
}

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_bmp(char const *filename, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_bmp_core(&s, x, y, comp, data);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif //!STBI_WRITE_NO_STDIO

static int stbi_write_tga_core(stbi__write_context *s, int x, int y, int comp, void *data)
{
   int has_alpha = (comp == 2 || comp == 4);
   int colorbytes = has_alpha ? comp-1 : comp;
   int format = colorbytes < 2 ? 3 : 2; // 3 color channels (RGB/RGBA) = 2, 1 color channel (Y/YA) = 3

   if (y < 0 || x < 0)
      return 0;

   if (!stbi_write_tga_with_rle) {
      return stbiw__outfile(s, -1, -1, x, y, comp, 0, (void *) data, has_alpha, 0,
         "111 221 2222 11", 0, 0, format, 0, 0, 0, 0, 0, x, y, (colorbytes + has_alpha) * 8, has_alpha * 8);
   } else {
      int i,j,k;
      int jend, jdir;

      stbiw__writef(s, "111 221 2222 11", 0,0,format+8, 0,0,0, 0,0,x,y, (colorbytes + has_alpha) * 8, has_alpha * 8);

      if (stbi__flip_vertically_on_write) {
         j = 0;
         jend = y;
         jdir = 1;
      } else {
         j = y-1;
         jend = -1;
         jdir = -1;
      }
      for (; j != jend; j += jdir) {
         unsigned char *row = (unsigned char *) data + j * x * comp;
         int len;

         for (i = 0; i < x; i += len) {
            unsigned char *begin = row + i * comp;
            int diff = 1;
            len = 1;

            if (i < x - 1) {
               ++len;
               diff = memcmp(begin, row + (i + 1) * comp, comp);
               if (diff) {
                  const unsigned char *prev = begin;
                  for (k = i + 2; k < x && len < 128; ++k) {
                     if (memcmp(prev, row + k * comp, comp)) {
                        prev += comp;
                        ++len;
                     } else {
                        --len;
                        break;
                     }
                  }
               } else {
                  for (k = i + 2; k < x && len < 128; ++k) {
                     if (!memcmp(begin, row + k * comp, comp)) {
                        ++len;
                     } else {
                        break;
                     }
                  }
               }
            }

            if (diff) {
               unsigned char header = STBIW_UCHAR(len - 1);
               stbiw__write1(s, header);
               for (k = 0; k < len; ++k) {
                  stbiw__write_pixel(s, -1, comp, has_alpha, 0, begin + k * comp);
               }
            } else {
               unsigned char header = STBIW_UCHAR(len - 129);
               stbiw__write1(s, header);
               stbiw__write_pixel(s, -1, comp, has_alpha, 0, begin);
            }
         }
      }
      stbiw__write_flush(s);
   }
   return 1;
}

STBIWDEF int stbi_write_tga_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_tga_core(&s, x, y, comp, (void *) data);
}

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_tga(char const *filename, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_tga_core(&s, x, y, comp, (void *) data);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif

// *************************************************************************************************
// Radiance RGBE HDR writer
// by Baldur Karlsson

#define stbiw__max(a, b)  ((a) > (b) ? (a) : (b))

#ifndef STBI_WRITE_NO_STDIO

static void stbiw__linear_to_rgbe(unsigned char *rgbe, float *linear)
{
   int exponent;
   float maxcomp = stbiw__max(linear[0], stbiw__max(linear[1], linear[2]));

   if (maxcomp < 1e-32f) {
      rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
   } else {
      float normalize = (float) frexp(maxcomp, &exponent) * 256.0f/maxcomp;

      rgbe[0] = (unsigned char)(linear[0] * normalize);
      rgbe[1] = (unsigned char)(linear[1] * normalize);
      rgbe[2] = (unsigned char)(linear[2] * normalize);
      rgbe[3] = (unsigned char)(exponent + 128);
   }
}

static void stbiw__write_run_data(stbi__write_context *s, int length, unsigned char databyte)
{
   unsigned char lengthbyte = STBIW_UCHAR(length+128);
   STBIW_ASSERT(length+128 <= 255);
   s->func(s->context, &lengthbyte, 1);
   s->func(s->context, &databyte, 1);
}

static void stbiw__write_dump_data(stbi__write_context *s, int length, unsigned char *data)
{
   unsigned char lengthbyte = STBIW_UCHAR(length);
   STBIW_ASSERT(length <= 128); // inconsistent with spec but consistent with official code
   s->func(s->context, &lengthbyte, 1);
   s->func(s->context, data, length);
}

static void stbiw__write_hdr_scanline(stbi__write_context *s, int width, int ncomp, unsigned char *scratch, float *scanline)
{
   unsigned char scanlineheader[4] = { 2, 2, 0, 0 };
   unsigned char rgbe[4];
   float linear[3];
   int x;

   scanlineheader[2] = (width&0xff00)>>8;
   scanlineheader[3] = (width&0x00ff);

   /* skip RLE for images too small or large */
   if (width < 8 || width >= 32768) {
      for (x=0; x < width; x++) {
         switch (ncomp) {
            case 4: /* fallthrough */
            case 3: linear[2] = scanline[x*ncomp + 2];
                    linear[1] = scanline[x*ncomp + 1];
                    linear[0] = scanline[x*ncomp + 0];
                    break;
            default:
                    linear[0] = linear[1] = linear[2] = scanline[x*ncomp + 0];
                    break;
         }
         stbiw__linear_to_rgbe(rgbe, linear);
         s->func(s->context, rgbe, 4);
      }
   } else {
      int c,r;
      /* encode into scratch buffer */
      for (x=0; x < width; x++) {
         switch(ncomp) {
            case 4: /* fallthrough */
            case 3: linear[2] = scanline[x*ncomp + 2];
                    linear[1] = scanline[x*ncomp + 1];
                    linear[0] = scanline[x*ncomp + 0];
                    break;
            default:
                    linear[0] = linear[1] = linear[2] = scanline[x*ncomp + 0];
                    break;
         }
         stbiw__linear_to_rgbe(rgbe, linear);
         scratch[x + width*0] = rgbe[0];
         scratch[x + width*1] = rgbe[1];
         scratch[x + width*2] = rgbe[2];
         scratch[x + width*3] = rgbe[3];
      }

      s->func(s->context, scanlineheader, 4);

      /* RLE each component separately */
      for (c=0; c < 4; c++) {
         unsigned char *comp = &scratch[width*c];

         x = 0;
         while (x < width) {
            // find first run
            r = x;
            while (r+2 < width) {
               if (comp[r] == comp[r+1] && comp[r] == comp[r+2])
                  break;
               ++r;
            }
            if (r+2 >= width)
               r = width;
            // dump up to first run
            while (x < r) {
               int len = r-x;
               if (len > 128) len = 128;
               stbiw__write_dump_data(s, len, &comp[x]);
               x += len;
            }
            // if there's a run, output it
            if (r+2 < width) { // same test as what we break out of in search loop, so only true if we break'd
               // find next byte after run
               while (r < width && comp[r] == comp[x])
                  ++r;
               // output run up to r
               while (x < r) {
                  int len = r-x;
                  if (len > 127) len = 127;
                  stbiw__write_run_data(s, len, comp[x]);
                  x += len;
               }
            }
         }
      }
   }
}

static int stbi_write_hdr_core(stbi__write_context *s, int x, int y, int comp, float *data)
{
   if (y <= 0 || x <= 0 || data == NULL)
      return 0;
   else {
      // Each component is stored separately. Allocate scratch space for full output scanline.
      unsigned char *scratch = (unsigned char *) STBIW_MALLOC(x*4);
      int i, len;
      char buffer[128];
      char header[] = "#?RADIANCE\n# Written by stb_image_write.h\nFORMAT=32-bit_rle_rgbe\n";
      s->func(s->context, header, sizeof(header)-1);

#ifdef __STDC_LIB_EXT1__
      len = sprintf_s(buffer, sizeof(buffer), "EXPOSURE=          1.0000000000000\n\n-Y %d +X %d\n", y, x);
#else
      len = sprintf(buffer, "EXPOSURE=          1.0000000000000\n\n-Y %d +X %d\n", y, x);
#endif
      s->func(s->context, buffer, len);

      for(i=0; i < y; i++)
         stbiw__write_hdr_scanline(s, x, comp, scratch, data + comp*x*(stbi__flip_vertically_on_write ? y-1-i : i));
      STBIW_FREE(scratch);
      return 1;
   }
}

STBIWDEF int stbi_write_hdr_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const float *data)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_hdr_core(&s, x, y, comp, (float *) data);
}

STBIWDEF int stbi_write_hdr(char const *filename, int x, int y, int comp, const float *data)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_hdr_core(&s, x, y, comp, (float *) data);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif // STBI_WRITE_NO_STDIO


//////////////////////////////////////////////////////////////////////////////
//
// PNG writer
//

#ifndef STBIW_ZLIB_COMPRESS

// ---------------------------------------------------------------------------
// Built-in zlib compressor.
//
//   * LZ77 with a 3-byte hash chain (fixed arrays, no per-bucket allocation)
//     and zlib-style lazy matching; `quality` sets how hard it searches.
//   * Dynamic Huffman blocks: every block gets its own length-limited
//     canonical code, and the cheapest of dynamic / fixed / stored is written.
//
// The output is a plain zlib stream that any inflater reads.

typedef unsigned long long stbiw__zu64;

#define stbiw__ZWINDOW    32768
#define stbiw__ZHBITS     16
#define stbiw__ZHSIZE     (1 << stbiw__ZHBITS)
#define stbiw__ZBLOCKTOK  65536   // tokens per deflate block; the Huffman codes are rebuilt for each block

static const unsigned short stbiw__zlenbase[29]   = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const unsigned char  stbiw__zlenextra[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const unsigned short stbiw__zdistbase[30]  = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const unsigned char  stbiw__zdistextra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static const unsigned char  stbiw__zclorder[19]   = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

typedef struct
{
   unsigned char *out;
   int n;                 // bytes written; capacity is reserved up front (see stbi_zlib_compress)
   stbiw__zu64 bits;
   int nbits;

   const unsigned char *data;
   unsigned int *tok;     // literal: the byte (< 256); match: (length << 16) | distance
   int ntok;
   int raw_start, raw_len;       // input bytes covered by the tokens of the current block
   int extra_bits;               // length / distance extra bits of the current block
   unsigned int lfreq[288], dfreq[32];

   unsigned short len_sym[259];  // match length -> literal/length symbol (257..285)
   unsigned char dtab[512];      // distance -> distance symbol (see stbiw__zdsym)
   unsigned char  fl[288], fdl[30];   // fixed Huffman code lengths
   unsigned short fc[288], fdc[30];   // fixed Huffman codes (bit-reversed for output)
} stbiw__zctx;

#define stbiw__zput(z, v, k) do { \
      (z)->bits |= (stbiw__zu64)(v) << (z)->nbits; (z)->nbits += (k); \
      if ((z)->nbits >= 32) { \
         stbiw__zu64 b_ = (z)->bits; unsigned char *o_ = (z)->out + (z)->n; \
         o_[0] = (unsigned char) b_; o_[1] = (unsigned char) (b_ >> 8); \
         o_[2] = (unsigned char) (b_ >> 16); o_[3] = (unsigned char) (b_ >> 24); \
         (z)->n += 4; (z)->bits >>= 32; (z)->nbits -= 32; } \
   } while (0)

static void stbiw__zalign(stbiw__zctx *z)
{
   while (z->nbits > 0) {
      z->out[z->n++] = (unsigned char) z->bits;
      z->bits >>= 8;
      z->nbits -= 8;
   }
   z->bits = 0;
   z->nbits = 0;
}

static int stbiw__zlib_bitrev(int code, int codebits)
{
   int res=0;
   while (codebits--) {
      res = (res << 1) | (code & 1);
      code >>= 1;
   }
   return res;
}

static int stbiw__zcmp_u32(const void *a, const void *b)
{
   unsigned int x = *(const unsigned int *) a, y = *(const unsigned int *) b;
   return x < y ? -1 : x > y ? 1 : 0;
}

// Code lengths (all <= maxlen) for freq[0..n-1]. At least two symbols must have a
// non-zero frequency, and n <= 288. Huffman by the two-queue method; if the tree
// is too deep the frequencies are flattened and it is rebuilt (slightly
// suboptimal, always valid).
static void stbiw__zhuff_lengths(const unsigned int *freq, int n, int maxlen, unsigned char *len)
{
   unsigned int work[288], key[288], w[600];
   int parent[600], depth[600];
   int i, k, m, li, ni, nn, maxd;
   for (i=0; i < n; ++i) work[i] = freq[i];
   for (;;) {
      m = 0;
      for (i=0; i < n; ++i) if (work[i]) key[m++] = (work[i] << 9) | (unsigned int) i;
      qsort(key, (size_t) m, sizeof(key[0]), stbiw__zcmp_u32);
      for (i=0; i < m; ++i) w[i] = key[i] >> 9;
      li = 0; ni = m; nn = m;
      for (k=0; k < m-1; ++k) {
         int a, b;
         if (li < m && (ni >= nn || w[li] <= w[ni])) a = li++; else a = ni++;
         if (li < m && (ni >= nn || w[li] <= w[ni])) b = li++; else b = ni++;
         w[nn] = w[a] + w[b];
         parent[a] = parent[b] = nn;
         ++nn;
      }
      depth[nn-1] = 0;
      maxd = 0;
      for (i=nn-2; i >= 0; --i) {
         depth[i] = depth[parent[i]] + 1;
         if (i < m && depth[i] > maxd) maxd = depth[i];
      }
      if (maxd <= maxlen) break;
      for (i=0; i < n; ++i) if (work[i]) work[i] = (work[i] + 1) >> 1;
   }
   for (i=0; i < n; ++i) len[i] = 0;
   for (i=0; i < m; ++i) len[key[i] & 511] = (unsigned char) depth[i];
}

// Canonical codes for the given lengths, bit-reversed so they can be written LSB first.
static void stbiw__zhuff_codes(const unsigned char *len, int n, unsigned short *code)
{
   int count[16], next[16], i, c = 0;
   for (i=0; i < 16; ++i) count[i] = 0;
   for (i=0; i < n; ++i) count[len[i]]++;
   count[0] = 0;
   next[0] = 0;
   for (i=1; i < 16; ++i) { c = (c + count[i-1]) << 1; next[i] = c; }
   for (i=0; i < n; ++i)
      code[i] = len[i] ? (unsigned short) stbiw__zlib_bitrev(next[len[i]]++, len[i]) : 0;
}

#define stbiw__zdsym(c, d) ((d) <= 256 ? (c)->dtab[(d)-1] : (c)->dtab[256 + (((d)-1) >> 7)])

static void stbiw__zwrite_tokens(stbiw__zctx *c, const unsigned short *lc, const unsigned char *ll,
                                 const unsigned short *dc, const unsigned char *dl)
{
   int i;
   for (i=0; i < c->ntok; ++i) {
      unsigned int t = c->tok[i];
      if (t < 256) {
         stbiw__zput(c, lc[t], ll[t]);
      } else {
         int len = (int) (t >> 16), dist = (int) (t & 0xffff);
         int ls = c->len_sym[len], ds = stbiw__zdsym(c, dist);
         stbiw__zput(c, lc[ls], ll[ls]);
         if (stbiw__zlenextra[ls-257]) stbiw__zput(c, len - stbiw__zlenbase[ls-257], stbiw__zlenextra[ls-257]);
         stbiw__zput(c, dc[ds], dl[ds]);
         if (stbiw__zdistextra[ds]) stbiw__zput(c, dist - stbiw__zdistbase[ds], stbiw__zdistextra[ds]);
      }
   }
   stbiw__zput(c, lc[256], ll[256]);
}

// Writes the tokens collected so far as one block (the cheapest of dynamic, fixed, stored).
static void stbiw__zflush(stbiw__zctx *c, int final)
{
   unsigned int lf[288], df[32], clf[19];
   unsigned char ll[288], dl[32], cl[19], rle[320], rlx[320], seq[320];
   unsigned short lc[288], dc[32], cc[19];
   int i, used, nlit, ndist, ncl, nrle, total, run, v, r, nsub;
   unsigned int dyn, fix, sto;

   c->lfreq[256] = 1;   // end of block

   for (i=0; i < 286; ++i) lf[i] = c->lfreq[i];
   for (i=0; i < 30; ++i) df[i] = c->dfreq[i];
   for (used=0, i=0; i < 286; ++i) used += lf[i] != 0;
   for (i=0; i < 286 && used < 2; ++i) if (!lf[i]) { lf[i] = 1; ++used; }
   for (used=0, i=0; i < 30; ++i) used += df[i] != 0;
   for (i=0; i < 30 && used < 2; ++i) if (!df[i]) { df[i] = 1; ++used; }
   stbiw__zhuff_lengths(lf, 286, 15, ll);
   stbiw__zhuff_lengths(df, 30, 15, dl);
   stbiw__zhuff_codes(ll, 286, lc);
   stbiw__zhuff_codes(dl, 30, dc);

   nlit = 286; while (nlit > 257 && ll[nlit-1] == 0) --nlit;
   ndist = 30; while (ndist > 1 && dl[ndist-1] == 0) --ndist;

   // run-length code the code lengths (16: repeat previous, 17/18: runs of zeros)
   total = nlit + ndist;
   for (i=0; i < nlit; ++i) seq[i] = ll[i];
   for (i=0; i < ndist; ++i) seq[nlit+i] = dl[i];
   nrle = 0;
   for (i=0; i < total; ) {
      v = seq[i];
      run = 1;
      while (i+run < total && seq[i+run] == v) ++run;
      if (v == 0 && run >= 3) {
         i += run;
         while (run > 0) {
            if (run >= 11) { r = run > 138 ? 138 : run; rle[nrle] = 18; rlx[nrle++] = (unsigned char) (r-11); run -= r; }
            else if (run >= 3) { rle[nrle] = 17; rlx[nrle++] = (unsigned char) (run-3); run = 0; }
            else { while (run--) { rle[nrle] = 0; rlx[nrle++] = 0; } run = 0; }
         }
      } else {
         rle[nrle] = (unsigned char) v; rlx[nrle++] = 0;
         ++i; --run;
         while (run >= 3) { r = run > 6 ? 6 : run; rle[nrle] = 16; rlx[nrle++] = (unsigned char) (r-3); run -= r; i += r; }
         while (run > 0) { rle[nrle] = (unsigned char) v; rlx[nrle++] = 0; --run; ++i; }
      }
   }
   for (i=0; i < 19; ++i) clf[i] = 0;
   for (i=0; i < nrle; ++i) clf[rle[i]]++;
   for (used=0, i=0; i < 19; ++i) used += clf[i] != 0;
   for (i=0; i < 19 && used < 2; ++i) if (!clf[i]) { clf[i] = 1; ++used; }
   stbiw__zhuff_lengths(clf, 19, 7, cl);
   stbiw__zhuff_codes(cl, 19, cc);
   ncl = 19; while (ncl > 4 && cl[stbiw__zclorder[ncl-1]] == 0) --ncl;

   // what would each kind of block cost, in bits?
   dyn = 3 + 14 + 3 * (unsigned int) ncl + (unsigned int) c->extra_bits;
   for (i=0; i < nrle; ++i) dyn += cl[rle[i]] + (rle[i] == 16 ? 2 : rle[i] == 17 ? 3 : rle[i] == 18 ? 7 : 0);
   for (i=0; i < 286; ++i) dyn += c->lfreq[i] * ll[i];
   for (i=0; i < 30; ++i) dyn += c->dfreq[i] * dl[i];
   fix = 3 + (unsigned int) c->extra_bits;
   for (i=0; i < 286; ++i) fix += c->lfreq[i] * c->fl[i];
   for (i=0; i < 30; ++i) fix += c->dfreq[i] * 5;
   nsub = c->raw_len ? (c->raw_len + 65534) / 65535 : 1;
   sto = (unsigned int) c->raw_len * 8 + (unsigned int) nsub * 48;

   if (sto <= dyn && sto <= fix) {
      const unsigned char *p = c->data + c->raw_start;
      int left = c->raw_len;
      for (i=0; i < nsub; ++i) {
         int blk = left > 65535 ? 65535 : left;
         stbiw__zput(c, final && i == nsub-1, 1);
         stbiw__zput(c, 0, 2);
         stbiw__zalign(c);
         c->out[c->n++] = (unsigned char) blk;        c->out[c->n++] = (unsigned char) (blk >> 8);
         c->out[c->n++] = (unsigned char) ~blk;       c->out[c->n++] = (unsigned char) (~blk >> 8);
         memcpy(c->out + c->n, p, (size_t) blk);
         c->n += blk; p += blk; left -= blk;
      }
   } else if (fix <= dyn) {
      stbiw__zput(c, final, 1);
      stbiw__zput(c, 1, 2);
      stbiw__zwrite_tokens(c, c->fc, c->fl, c->fdc, c->fdl);
   } else {
      stbiw__zput(c, final, 1);
      stbiw__zput(c, 2, 2);
      stbiw__zput(c, nlit - 257, 5);
      stbiw__zput(c, ndist - 1, 5);
      stbiw__zput(c, ncl - 4, 4);
      for (i=0; i < ncl; ++i) stbiw__zput(c, cl[stbiw__zclorder[i]], 3);
      for (i=0; i < nrle; ++i) {
         stbiw__zput(c, cc[rle[i]], cl[rle[i]]);
         if (rle[i] == 16) stbiw__zput(c, rlx[i], 2);
         else if (rle[i] == 17) stbiw__zput(c, rlx[i], 3);
         else if (rle[i] == 18) stbiw__zput(c, rlx[i], 7);
      }
      stbiw__zwrite_tokens(c, lc, ll, dc, dl);
   }

   c->raw_start += c->raw_len;
   c->raw_len = 0;
   c->ntok = 0;
   c->extra_bits = 0;
   memset(c->lfreq, 0, sizeof(c->lfreq));
   memset(c->dfreq, 0, sizeof(c->dfreq));
}

static void stbiw__zadd_lit(stbiw__zctx *c, int b)
{
   c->tok[c->ntok++] = (unsigned int) b;
   c->lfreq[b]++;
   c->raw_len++;
   if (c->ntok >= stbiw__ZBLOCKTOK) stbiw__zflush(c, 0);
}

static void stbiw__zadd_match(stbiw__zctx *c, int len, int dist)
{
   int ls = c->len_sym[len], ds = stbiw__zdsym(c, dist);
   c->tok[c->ntok++] = ((unsigned int) len << 16) | (unsigned int) dist;
   c->lfreq[ls]++;
   c->dfreq[ds]++;
   c->extra_bits += stbiw__zlenextra[ls-257] + stbiw__zdistextra[ds];
   c->raw_len += len;
   if (c->ntok >= stbiw__ZBLOCKTOK) stbiw__zflush(c, 0);
}

static unsigned int stbiw__zload32(const unsigned char *p)
{
   unsigned int v;
   memcpy(&v, p, 4);
   return v;
}

static int stbiw__zmatchlen(const unsigned char *a, const unsigned char *b, int maxlen)
{
   int l = 0;
   while (l + 8 <= maxlen) {
      stbiw__zu64 x, y;
      memcpy(&x, a+l, 8); memcpy(&y, b+l, 8);
      if (x != y) break;
      l += 8;
   }
   while (l < maxlen && a[l] == b[l]) ++l;
   return l;
}

#define stbiw__zhash4(p) (stbiw__zload32(p) * 0x9E3779B1u >> (32 - stbiw__ZHBITS))

// Longest match for d[pos..] that is longer than `best`; *dist is set when one is found.
static int stbiw__zfind(const unsigned char *d, int pos, int n, const int *head, const int *prev,
                        int best, int chain, int nice, int *dist)
{
   int maxlen = n - pos, cand;
   const unsigned char *b = d + pos;
   if (maxlen > 258) maxlen = 258;
   if (nice > maxlen) nice = maxlen;
   if (best >= maxlen) return best;
   cand = head[stbiw__zhash4(b)];
   while (cand >= 0 && pos - cand <= stbiw__ZWINDOW && chain-- > 0) {
      const unsigned char *a = d + cand;
      if (a[best] == b[best] && stbiw__zload32(a) == stbiw__zload32(b)) {
         int l = stbiw__zmatchlen(a, b, maxlen);
         if (l > best) {
            best = l;
            *dist = pos - cand;
            if (l >= nice) break;
         }
      }
      {
         int next = prev[cand & (stbiw__ZWINDOW-1)];
         if (next >= cand) break;
         cand = next;
      }
   }
   return best;
}

#endif // STBIW_ZLIB_COMPRESS

STBIWDEF unsigned char * stbi_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality)
{
#ifdef STBIW_ZLIB_COMPRESS
   // user provided a zlib compress implementation, use that
   return STBIW_ZLIB_COMPRESS(data, data_len, out_len, quality);
#else // use builtin
   // search effort by quality: { good length, max lazy length (0 = greedy), nice length, max chain }
   static const unsigned short effort[11][4] = {
      {4,0,8,4}, {4,0,8,4}, {4,0,16,8}, {4,0,32,32}, {4,4,16,16}, {8,16,32,32},
      {8,16,64,64}, {8,16,128,128}, {8,32,128,256}, {32,128,258,1024}, {32,258,258,4096}
   };
   stbiw__zctx *c;
   int *head, *prev;
   int i, j, pos, cap, good, lazy, nice, maxchain;
   int pend_len = 0, pend_dist = 0, pend_lit = 0;

   if (quality < 0) quality = 0;
   if (quality > 10) quality = 10;
   good = effort[quality][0]; lazy = effort[quality][1]; nice = effort[quality][2]; maxchain = effort[quality][3];

   c = (stbiw__zctx *) STBIW_MALLOC(sizeof(*c));
   head = (int *) STBIW_MALLOC(2 * stbiw__ZHSIZE * sizeof(int));
   // worst case: every block ends up stored (5 bytes of header per 64K) plus the zlib wrapper
   cap = data_len + data_len / 4096 + 1024;
   if (!c || !head || !(c->tok = (unsigned int *) STBIW_MALLOC(stbiw__ZBLOCKTOK * sizeof(unsigned int)))
       || !(c->out = (unsigned char *) STBIW_MALLOC((size_t) cap))) {
      if (c) { STBIW_FREE(c->tok); STBIW_FREE(c); }
      STBIW_FREE(head);
      return NULL;
   }
   prev = head + stbiw__ZHSIZE;
   memset(head, 0xff, stbiw__ZHSIZE * sizeof(int));   // -1: empty

   c->data = data;
   c->n = 0; c->bits = 0; c->nbits = 0;
   c->ntok = 0; c->raw_start = 0; c->raw_len = 0; c->extra_bits = 0;
   memset(c->lfreq, 0, sizeof(c->lfreq));
   memset(c->dfreq, 0, sizeof(c->dfreq));

   // lookup tables: length -> symbol, distance -> symbol, fixed Huffman code
   for (i=0; i < 3; ++i) c->len_sym[i] = 257;   // never used: lengths start at 3
   for (j=0; j < 29; ++j) {
      int hi = j < 28 ? stbiw__zlenbase[j+1] - 1 : 258;
      for (i=stbiw__zlenbase[j]; i <= hi; ++i) c->len_sym[i] = (unsigned short) (257 + j);
   }
   for (j=0; j < 30; ++j) {
      int lo = stbiw__zdistbase[j] - 1, hi = lo + (1 << stbiw__zdistextra[j]) - 1;
      if (hi < 256) for (i=lo; i <= hi; ++i) c->dtab[i] = (unsigned char) j;
      else for (i=lo >> 7; i <= hi >> 7; ++i) c->dtab[256+i] = (unsigned char) j;
      c->fdl[j] = 5;
      c->fdc[j] = (unsigned short) stbiw__zlib_bitrev(j, 5);
   }
   for (i=0; i < 288; ++i) c->fl[i] = (unsigned char) (i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
   stbiw__zhuff_codes(c->fl, 288, c->fc);

   c->out[c->n++] = 0x78;   // zlib header: deflate, 32K window
   c->out[c->n++] = 0x5e;

   pos = 0;
   while (pos < data_len) {
      int len = 0, dist = 0, have = data_len - pos >= 4;
      if (have && pend_len < lazy + (lazy == 0)) {
         int floor = pend_len > 3 ? pend_len : 3;
         len = stbiw__zfind(data, pos, data_len, head, prev, floor, pend_len >= good ? maxchain >> 2 : maxchain, nice, &dist);
         if (len <= floor) len = 0;
      }
      if (lazy == 0) {
         // greedy
         if (len >= 4) {
            stbiw__zadd_match(c, len, dist);
            for (i=0; i < len; ++i, ++pos)
               if (data_len - pos >= 4) { int h = (int) stbiw__zhash4(data+pos); prev[pos & (stbiw__ZWINDOW-1)] = head[h]; head[h] = pos; }
         } else {
            stbiw__zadd_lit(c, data[pos]);
            if (have) { int h = (int) stbiw__zhash4(data+pos); prev[pos & (stbiw__ZWINDOW-1)] = head[h]; head[h] = pos; }
            ++pos;
         }
         continue;
      }
      if (have) { int h = (int) stbiw__zhash4(data+pos); prev[pos & (stbiw__ZWINDOW-1)] = head[h]; head[h] = pos; }
      if (pend_len >= 4 && len <= pend_len) {
         // the match found one byte ago wins
         int end = pos - 1 + pend_len;
         stbiw__zadd_match(c, pend_len, pend_dist);
         for (i=pos+1; i < end; ++i)
            if (data_len - i >= 4) { int h = (int) stbiw__zhash4(data+i); prev[i & (stbiw__ZWINDOW-1)] = head[h]; head[h] = i; }
         pos = end;
         pend_len = 0; pend_lit = 0;
      } else {
         if (pend_lit) stbiw__zadd_lit(c, data[pos-1]);
         pend_len = len; pend_dist = dist; pend_lit = 1;
         ++pos;
      }
   }
   if (pend_lit) stbiw__zadd_lit(c, data[pos-1]);
   stbiw__zflush(c, 1);
   stbiw__zalign(c);

   {
      // adler32 of the input, big endian
      unsigned int s1=1, s2=0;
      int blocklen = (int) (data_len % 5552);
      j=0;
      while (j < data_len) {
         for (i=0; i < blocklen; ++i) { s1 += data[j+i]; s2 += s1; }
         s1 %= 65521; s2 %= 65521;
         j += blocklen;
         blocklen = 5552;
      }
      c->out[c->n++] = STBIW_UCHAR(s2 >> 8);
      c->out[c->n++] = STBIW_UCHAR(s2);
      c->out[c->n++] = STBIW_UCHAR(s1 >> 8);
      c->out[c->n++] = STBIW_UCHAR(s1);
   }
   {
      unsigned char *result = c->out;
      *out_len = c->n;
      STBIW_FREE(c->tok);
      STBIW_FREE(c);
      STBIW_FREE(head);
      return result;
   }
#endif // STBIW_ZLIB_COMPRESS
}

static unsigned int stbiw__crc32(unsigned char *buffer, int len)
{
#ifdef STBIW_CRC32
    return STBIW_CRC32(buffer, len);
#else
   static unsigned int crc_table[256] =
   {
      0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
      0x0eDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
      0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
      0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
      0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
      0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
      0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
      0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
      0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
      0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
      0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
      0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
      0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
      0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
      0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
      0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
      0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
      0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
      0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
      0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
      0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
      0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
      0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
      0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
      0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
      0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
      0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
      0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
      0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
      0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
      0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
      0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
   };

   unsigned int crc = ~0u;
   int i;
   for (i=0; i < len; ++i)
      crc = (crc >> 8) ^ crc_table[buffer[i] ^ (crc & 0xff)];
   return ~crc;
#endif
}

#define stbiw__wpng4(o,a,b,c,d) ((o)[0]=STBIW_UCHAR(a),(o)[1]=STBIW_UCHAR(b),(o)[2]=STBIW_UCHAR(c),(o)[3]=STBIW_UCHAR(d),(o)+=4)
#define stbiw__wp32(data,v) stbiw__wpng4(data, (v)>>24,(v)>>16,(v)>>8,(v));
#define stbiw__wptag(data,s) stbiw__wpng4(data, s[0],s[1],s[2],s[3])

static void stbiw__wpcrc(unsigned char **data, int len)
{
   unsigned int crc = stbiw__crc32(*data - len - 4, len+4);
   stbiw__wp32(*data, crc);
}

static unsigned char stbiw__paeth(int a, int b, int c)
{
   int p = a + b - c, pa = abs(p-a), pb = abs(p-b), pc = abs(p-c);
   if (pa <= pb && pa <= pc) return STBIW_UCHAR(a);
   if (pb <= pc) return STBIW_UCHAR(b);
   return STBIW_UCHAR(c);
}

// @OPTIMIZE: provide an option that always forces left-predict or paeth predict
static void stbiw__encode_png_line(unsigned char *pixels, int stride_bytes, int width, int height, int y, int n, int filter_type, signed char *line_buffer)
{
   static int mapping[] = { 0,1,2,3,4 };
   static int firstmap[] = { 0,1,0,5,6 };
   int *mymap = (y != 0) ? mapping : firstmap;
   int i;
   int type = mymap[filter_type];
   unsigned char *z = pixels + stride_bytes * (stbi__flip_vertically_on_write ? height-1-y : y);
   int signed_stride = stbi__flip_vertically_on_write ? -stride_bytes : stride_bytes;

   if (type==0) {
      memcpy(line_buffer, z, width*n);
      return;
   }

   // first loop isn't optimized since it's just one pixel
   for (i = 0; i < n; ++i) {
      switch (type) {
         case 1: line_buffer[i] = z[i]; break;
         case 2: line_buffer[i] = z[i] - z[i-signed_stride]; break;
         case 3: line_buffer[i] = z[i] - (z[i-signed_stride]>>1); break;
         case 4: line_buffer[i] = (signed char) (z[i] - stbiw__paeth(0,z[i-signed_stride],0)); break;
         case 5: line_buffer[i] = z[i]; break;
         case 6: line_buffer[i] = z[i]; break;
      }
   }
   switch (type) {
      case 1: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - z[i-n]; break;
      case 2: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - z[i-signed_stride]; break;
      case 3: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - ((z[i-n] + z[i-signed_stride])>>1); break;
      case 4: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - stbiw__paeth(z[i-n], z[i-signed_stride], z[i-signed_stride-n]); break;
      case 5: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - (z[i-n]>>1); break;
      case 6: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - stbiw__paeth(z[i-n], 0,0); break;
   }
}

STBIWDEF unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len)
{
   int force_filter = stbi_write_force_png_filter;
   int ctype[5] = { -1, 0, 4, 2, 6 };
   unsigned char sig[8] = { 137,80,78,71,13,10,26,10 };
   unsigned char *out,*o, *filt, *zlib;
   signed char *line_buffer;
   int j,zlen;

   if (stride_bytes == 0)
      stride_bytes = x * n;

   if (force_filter >= 5) {
      force_filter = -1;
   }

   filt = (unsigned char *) STBIW_MALLOC((x*n+1) * y); if (!filt) return 0;
   line_buffer = (signed char *) STBIW_MALLOC(x * n); if (!line_buffer) { STBIW_FREE(filt); return 0; }
   for (j=0; j < y; ++j) {
      int filter_type;
      if (force_filter > -1) {
         filter_type = force_filter;
         stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, force_filter, line_buffer);
      } else { // Estimate the best filter by running through all of them:
         int best_filter = 0, best_filter_val = 0x7fffffff, est, i;
         for (filter_type = 0; filter_type < 5; filter_type++) {
            stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, filter_type, line_buffer);

            // Estimate the entropy of the line using this filter; the less, the better.
            est = 0;
            for (i = 0; i < x*n; ++i) {
               est += abs((signed char) line_buffer[i]);
            }
            if (est < best_filter_val) {
               best_filter_val = est;
               best_filter = filter_type;
            }
         }
         if (filter_type != best_filter) {  // If the last iteration already got us the best filter, don't redo it
            stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, best_filter, line_buffer);
            filter_type = best_filter;
         }
      }
      // when we get here, filter_type contains the filter type, and line_buffer contains the data
      filt[j*(x*n+1)] = (unsigned char) filter_type;
      STBIW_MEMMOVE(filt+j*(x*n+1)+1, line_buffer, x*n);
   }
   STBIW_FREE(line_buffer);
   zlib = stbi_zlib_compress(filt, y*( x*n+1), &zlen, stbi_write_png_compression_level);
   STBIW_FREE(filt);
   if (!zlib) return 0;

   // each tag requires 12 bytes of overhead
   out = (unsigned char *) STBIW_MALLOC(8 + 12+13 + 12+zlen + 12);
   if (!out) return 0;
   *out_len = 8 + 12+13 + 12+zlen + 12;

   o=out;
   STBIW_MEMMOVE(o,sig,8); o+= 8;
   stbiw__wp32(o, 13); // header length
   stbiw__wptag(o, "IHDR");
   stbiw__wp32(o, x);
   stbiw__wp32(o, y);
   *o++ = 8;
   *o++ = STBIW_UCHAR(ctype[n]);
   *o++ = 0;
   *o++ = 0;
   *o++ = 0;
   stbiw__wpcrc(&o,13);

   stbiw__wp32(o, zlen);
   stbiw__wptag(o, "IDAT");
   STBIW_MEMMOVE(o, zlib, zlen);
   o += zlen;
   STBIW_FREE(zlib);
   stbiw__wpcrc(&o, zlen);

   stbiw__wp32(o,0);
   stbiw__wptag(o, "IEND");
   stbiw__wpcrc(&o,0);

   STBIW_ASSERT(o == out + *out_len);

   return out;
}

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_png(char const *filename, int x, int y, int comp, const void *data, int stride_bytes)
{
   FILE *f;
   int len;
   unsigned char *png = stbi_write_png_to_mem((const unsigned char *) data, stride_bytes, x, y, comp, &len);
   if (png == NULL) return 0;

   f = stbiw__fopen(filename, "wb");
   if (!f) { STBIW_FREE(png); return 0; }
   fwrite(png, 1, len, f);
   fclose(f);
   STBIW_FREE(png);
   return 1;
}
#endif

STBIWDEF int stbi_write_png_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data, int stride_bytes)
{
   int len;
   unsigned char *png = stbi_write_png_to_mem((const unsigned char *) data, stride_bytes, x, y, comp, &len);
   if (png == NULL) return 0;
   func(context, png, len);
   STBIW_FREE(png);
   return 1;
}


/* ***************************************************************************
 *
 * JPEG writer
 *
 * This is based on Jon Olick's jo_jpeg.cpp:
 * public domain Simple, Minimalistic JPEG writer - http://www.jonolick.com/code.html
 */

static const unsigned char stbiw__jpg_ZigZag[] = { 0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,
      24,31,40,44,53,10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63 };

// libstb patch: the entropy bit writer. Upstream called the sink once per OUTPUT BYTE; this
// one keeps a 64-bit accumulator and a 4 KB buffer (one sink call per 4 KB), and stores four
// bytes at a time unless one of them is 0xFF (which must be followed by a stuffed 0x00).
// It writes exactly the bytes upstream does.
typedef struct {
   stbi__write_context *s;
   unsigned long long acc;
   int nbits;
   int len;
   unsigned char buf[4096 + 16];
} stbiw__jpg_bw;

static void stbiw__jpg_bw_flush(stbiw__jpg_bw *w)
{
   if (w->len) {
      w->s->func(w->s->context, w->buf, w->len);
      w->len = 0;
   }
}

static void stbiw__jpg_bw_put32(stbiw__jpg_bw *w, unsigned int v)
{
   unsigned int t = ~v;
   if (((t - 0x01010101u) & ~t & 0x80808080u) == 0) {   // none of the four bytes is 0xFF
      unsigned char *o = w->buf + w->len;
      o[0] = (unsigned char) (v >> 24); o[1] = (unsigned char) (v >> 16);
      o[2] = (unsigned char) (v >> 8);  o[3] = (unsigned char) v;
      w->len += 4;
   } else {
      int i;
      for (i = 24; i >= 0; i -= 8) {
         unsigned char c = (unsigned char) (v >> i);
         w->buf[w->len++] = c;
         if (c == 0xFF) w->buf[w->len++] = 0;
      }
   }
   if (w->len >= 4096) stbiw__jpg_bw_flush(w);
}

// Appends the low n bits of `bits` (n <= 27, bits above n must be zero).
static void stbiw__jpg_bw_put(stbiw__jpg_bw *w, unsigned int bits, int n)
{
   w->acc = (w->acc << n) | bits;
   w->nbits += n;
   if (w->nbits >= 32) {
      w->nbits -= 32;
      stbiw__jpg_bw_put32(w, (unsigned int) (w->acc >> w->nbits));
   }
}

// Pads the last byte with 1 bits (what upstream's fill bits do) and writes everything out.
static void stbiw__jpg_bw_finish(stbiw__jpg_bw *w)
{
   int pad = (8 - (w->nbits & 7)) & 7;
   if (pad) {
      w->acc = (w->acc << pad) | ((1u << pad) - 1);
      w->nbits += pad;
   }
   while (w->nbits >= 8) {
      unsigned char c;
      w->nbits -= 8;
      c = (unsigned char) (w->acc >> w->nbits);
      w->buf[w->len++] = c;
      if (c == 0xFF) w->buf[w->len++] = 0;
   }
   stbiw__jpg_bw_flush(w);
}

// libstb patch: replaces upstream's stbiw__jpg_DCT with the same AAN DCT (identical arithmetic, in the
// same order, so the output bytes do not change), applied to 8 columns of a packed
// 8x8 block at once. Lane i works on p[i], p[i+8], ... p[i+56]; the 8 iterations are
// independent, so compilers turn the loop into SIMD (SSE2 / NEON) by themselves.
static void stbiw__jpg_dct_cols(float *p) {
   int i;
   for(i = 0; i < 8; ++i) {
      float d0 = p[i], d1 = p[i+8], d2 = p[i+16], d3 = p[i+24];
      float d4 = p[i+32], d5 = p[i+40], d6 = p[i+48], d7 = p[i+56];
      float z1, z2, z3, z4, z5, z11, z13;
      float tmp0 = d0 + d7;
      float tmp7 = d0 - d7;
      float tmp1 = d1 + d6;
      float tmp6 = d1 - d6;
      float tmp2 = d2 + d5;
      float tmp5 = d2 - d5;
      float tmp3 = d3 + d4;
      float tmp4 = d3 - d4;
      float tmp10 = tmp0 + tmp3;
      float tmp13 = tmp0 - tmp3;
      float tmp11 = tmp1 + tmp2;
      float tmp12 = tmp1 - tmp2;
      d0 = tmp10 + tmp11;
      d4 = tmp10 - tmp11;
      z1 = (tmp12 + tmp13) * 0.707106781f;
      d2 = tmp13 + z1;
      d6 = tmp13 - z1;
      tmp10 = tmp4 + tmp5;
      tmp11 = tmp5 + tmp6;
      tmp12 = tmp6 + tmp7;
      z5 = (tmp10 - tmp12) * 0.382683433f;
      z2 = tmp10 * 0.541196100f + z5;
      z4 = tmp12 * 1.306562965f + z5;
      z3 = tmp11 * 0.707106781f;
      z11 = tmp7 + z3;
      z13 = tmp7 - z3;
      p[i+40] = z13 + z2;
      p[i+24] = z13 - z2;
      p[i+8]  = z11 + z4;
      p[i+56] = z11 - z4;
      p[i] = d0;  p[i+16] = d2;  p[i+32] = d4;  p[i+48] = d6;
   }
}

// libstb patch: dst[x*8+y] = src[y*stride+x] for an 8x8 block (a pure data move, no arithmetic).
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define STBIW__JPG_SSE2 1
#include <emmintrin.h>
#endif

static void stbiw__jpg_transpose8(float *dst, const float *src, int stride)
{
#ifdef STBIW__JPG_SSE2
   int by, bx;
   for(by = 0; by < 8; by += 4)
      for(bx = 0; bx < 8; bx += 4) {
         const float *s0 = src + by*stride + bx;
         __m128 r0 = _mm_loadu_ps(s0), r1 = _mm_loadu_ps(s0 + stride);
         __m128 r2 = _mm_loadu_ps(s0 + 2*stride), r3 = _mm_loadu_ps(s0 + 3*stride);
         _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
         _mm_storeu_ps(dst + bx*8 + by,       r0);
         _mm_storeu_ps(dst + (bx+1)*8 + by,   r1);
         _mm_storeu_ps(dst + (bx+2)*8 + by,   r2);
         _mm_storeu_ps(dst + (bx+3)*8 + by,   r3);
      }
#else
   int x, y;
   for(y = 0; y < 8; ++y)
      for(x = 0; x < 8; ++x)
         dst[x*8+y] = src[y*stride+x];
#endif
}

// number of bits needed for a > 0
static int stbiw__jpg_nbits(unsigned int a)
{
#if defined(__GNUC__) || defined(__clang__)
   return 32 - __builtin_clz(a);
#else
   int n = 1;
   while (a >>= 1) ++n;
   return n;
#endif
}

static int stbiw__jpg_ctz64(unsigned long long x)   // x != 0
{
#if defined(__GNUC__) || defined(__clang__)
   return __builtin_ctzll(x);
#else
   int n = 0;
   while (!(x & 1)) { x >>= 1; ++n; }
   return n;
#endif
}

// Huffman code `ht` for `symbol`, immediately followed by the amplitude bits of `val`, in one put.
static void stbiw__jpg_put_value(stbiw__jpg_bw *w, const unsigned short *ht, int val, int nb)
{
   unsigned int amp = (unsigned int) (val < 0 ? val - 1 : val) & ((1u << nb) - 1);
   stbiw__jpg_bw_put(w, ((unsigned int) ht[0] << nb) | amp, ht[1] + nb);
}

static int stbiw__jpg_processDU(stbiw__jpg_bw *w, float *CDU, int du_stride, float *fdtbl, int DC, const unsigned short HTDC[256][2], const unsigned short HTAC[256][2]) {
   int i, diff;
   int DU[64];
   unsigned long long nz;

   // libstb patch: 2-D DCT as "row DCTs, then column DCTs" exactly like before, but each
   // pass runs through the 8-lane stbiw__jpg_dct_cols on a transposed copy of the block, and
   // the quantize/zigzag step is branch-free over contiguous arrays so it vectorizes too.
   {
      float A[64], B[64], Q[64];
      int Qi[64], k;
      stbiw__jpg_transpose8(A, CDU, du_stride);  // A[x*8+y] = CDU[y*du_stride+x]
      stbiw__jpg_dct_cols(A);                   // A[u*8+y]: horizontal frequency u of row y
      stbiw__jpg_transpose8(B, A, 8);           // B[y*8+x] = A[x*8+y]
      stbiw__jpg_dct_cols(B);                   // B[v*8+u]: vertical frequency v, horizontal u
      // Quantize/descale/zigzag the coefficients
      for(k = 0; k < 64; ++k) Q[k] = B[k]*fdtbl[k];
      for(k = 0; k < 64; ++k) Qi[k] = (int)(Q[k] < 0 ? Q[k] - 0.5f : Q[k] + 0.5f);
      for(k = 0; k < 64; ++k) DU[stbiw__jpg_ZigZag[k]] = Qi[k];
   }

   // Encode DC
   diff = DU[0] - DC;
   if (diff == 0) {
      stbiw__jpg_bw_put(w, HTDC[0][0], HTDC[0][1]);
   } else {
      stbiw__jpg_put_value(w, HTDC[stbiw__jpg_nbits((unsigned int) (diff < 0 ? -diff : diff))], diff,
                           stbiw__jpg_nbits((unsigned int) (diff < 0 ? -diff : diff)));
   }

   // Encode ACs: walk only the non-zero coefficients (bit k of nz is set when DU[k] != 0)
   nz = 0;
   for(i = 1; i < 64; ++i) nz |= (unsigned long long) (DU[i] != 0) << i;
   {
      int last = 0;   // index of the previous coefficient that was written (0 = the DC)
      while (nz) {
         int k = stbiw__jpg_ctz64(nz), zeroes = k - last - 1, val = DU[k];
         int nb = stbiw__jpg_nbits((unsigned int) (val < 0 ? -val : val));
         for (; zeroes >= 16; zeroes -= 16)
            stbiw__jpg_bw_put(w, HTAC[0xF0][0], HTAC[0xF0][1]);
         stbiw__jpg_put_value(w, HTAC[(zeroes << 4) + nb], val, nb);
         last = k;
         nz &= nz - 1;
      }
      if (last != 63) stbiw__jpg_bw_put(w, HTAC[0x00][0], HTAC[0x00][1]);   // EOB
   }
   return DU[0];
}

// libstb patch: RGB -> YCbCr for a run of n pixels inside the image (n is 8 or 16).
// The arithmetic is exactly the per-pixel float expression upstream uses, in the same order,
// so every rounding (and every output byte) is unchanged.
static void stbiw__jpg_convert_run(float *Y, float *U, float *V, const unsigned char *src, int ofsG, int ofsB, int comp, int n)
{
   int i = 0;
#ifdef STBIW__JPG_SSE2
   if(comp >= 3) {   // interleaved R,G,B[,A]: four pixels per step, SSE2 is part of the x86-64 baseline
      const __m128 cY0 = _mm_set1_ps(0.29900f), cY1 = _mm_set1_ps(0.58700f), cY2 = _mm_set1_ps(0.11400f), c128 = _mm_set1_ps(128);
      const __m128 cU0 = _mm_set1_ps(-0.16874f), cU1 = _mm_set1_ps(-0.33126f), cU2 = _mm_set1_ps(0.50000f);
      const __m128 cV0 = _mm_set1_ps(0.50000f), cV1 = _mm_set1_ps(-0.41869f), cV2 = _mm_set1_ps(-0.08131f);
      for(; i < n; i += 4) {
         __m128 r, g, b;
         if(comp == 4) {
            __m128i px = _mm_loadu_si128((const __m128i *)(src + i*4)), m = _mm_set1_epi32(255);
            r = _mm_cvtepi32_ps(_mm_and_si128(px, m));
            g = _mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(px, 8), m));
            b = _mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(px, 16), m));
         } else {
            unsigned int w0, w1, w2;   // r0 g0 b0 r1 | g1 b1 r2 g2 | b2 r3 g3 b3
            memcpy(&w0, src + i*3, 4); memcpy(&w1, src + i*3 + 4, 4); memcpy(&w2, src + i*3 + 8, 4);
            r = _mm_cvtepi32_ps(_mm_setr_epi32((int)(w0 & 255), (int)(w0 >> 24), (int)((w1 >> 16) & 255), (int)((w2 >> 8) & 255)));
            g = _mm_cvtepi32_ps(_mm_setr_epi32((int)((w0 >> 8) & 255), (int)(w1 & 255), (int)(w1 >> 24), (int)((w2 >> 16) & 255)));
            b = _mm_cvtepi32_ps(_mm_setr_epi32((int)((w0 >> 16) & 255), (int)((w1 >> 8) & 255), (int)(w2 & 255), (int)(w2 >> 24)));
         }
         _mm_storeu_ps(Y + i, _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(cY0, r), _mm_mul_ps(cY1, g)), _mm_mul_ps(cY2, b)), c128));
         _mm_storeu_ps(U + i, _mm_add_ps(_mm_add_ps(_mm_mul_ps(cU0, r), _mm_mul_ps(cU1, g)), _mm_mul_ps(cU2, b)));
         _mm_storeu_ps(V + i, _mm_add_ps(_mm_add_ps(_mm_mul_ps(cV0, r), _mm_mul_ps(cV1, g)), _mm_mul_ps(cV2, b)));
      }
      return;
   }
#endif
   for(; i < n; ++i) {
      float r = src[i*comp], g = src[i*comp+ofsG], b = src[i*comp+ofsB];
      Y[i]= +0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
      U[i]= -0.16874f*r - 0.33126f*g + 0.50000f*b;
      V[i]= +0.50000f*r - 0.41869f*g - 0.08131f*b;
   }
}

static int stbi_write_jpg_core(stbi__write_context *s, int width, int height, int comp, const void* data, int quality) {
   // Constants that don't pollute global namespace
   static const unsigned char std_dc_luminance_nrcodes[] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
   static const unsigned char std_dc_luminance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
   static const unsigned char std_ac_luminance_nrcodes[] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
   static const unsigned char std_ac_luminance_values[] = {
      0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
      0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
      0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
      0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
      0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
      0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
      0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
   };
   static const unsigned char std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
   static const unsigned char std_dc_chrominance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
   static const unsigned char std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
   static const unsigned char std_ac_chrominance_values[] = {
      0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
      0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
      0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
      0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
      0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
      0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
      0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
   };
   // Huffman tables
   static const unsigned short YDC_HT[256][2] = { {0,2},{2,3},{3,3},{4,3},{5,3},{6,3},{14,4},{30,5},{62,6},{126,7},{254,8},{510,9}};
   static const unsigned short UVDC_HT[256][2] = { {0,2},{1,2},{2,2},{6,3},{14,4},{30,5},{62,6},{126,7},{254,8},{510,9},{1022,10},{2046,11}};
   static const unsigned short YAC_HT[256][2] = {
      {10,4},{0,2},{1,2},{4,3},{11,4},{26,5},{120,7},{248,8},{1014,10},{65410,16},{65411,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {12,4},{27,5},{121,7},{502,9},{2038,11},{65412,16},{65413,16},{65414,16},{65415,16},{65416,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {28,5},{249,8},{1015,10},{4084,12},{65417,16},{65418,16},{65419,16},{65420,16},{65421,16},{65422,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {58,6},{503,9},{4085,12},{65423,16},{65424,16},{65425,16},{65426,16},{65427,16},{65428,16},{65429,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {59,6},{1016,10},{65430,16},{65431,16},{65432,16},{65433,16},{65434,16},{65435,16},{65436,16},{65437,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {122,7},{2039,11},{65438,16},{65439,16},{65440,16},{65441,16},{65442,16},{65443,16},{65444,16},{65445,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {123,7},{4086,12},{65446,16},{65447,16},{65448,16},{65449,16},{65450,16},{65451,16},{65452,16},{65453,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {250,8},{4087,12},{65454,16},{65455,16},{65456,16},{65457,16},{65458,16},{65459,16},{65460,16},{65461,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {504,9},{32704,15},{65462,16},{65463,16},{65464,16},{65465,16},{65466,16},{65467,16},{65468,16},{65469,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {505,9},{65470,16},{65471,16},{65472,16},{65473,16},{65474,16},{65475,16},{65476,16},{65477,16},{65478,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {506,9},{65479,16},{65480,16},{65481,16},{65482,16},{65483,16},{65484,16},{65485,16},{65486,16},{65487,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1017,10},{65488,16},{65489,16},{65490,16},{65491,16},{65492,16},{65493,16},{65494,16},{65495,16},{65496,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1018,10},{65497,16},{65498,16},{65499,16},{65500,16},{65501,16},{65502,16},{65503,16},{65504,16},{65505,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2040,11},{65506,16},{65507,16},{65508,16},{65509,16},{65510,16},{65511,16},{65512,16},{65513,16},{65514,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {65515,16},{65516,16},{65517,16},{65518,16},{65519,16},{65520,16},{65521,16},{65522,16},{65523,16},{65524,16},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2041,11},{65525,16},{65526,16},{65527,16},{65528,16},{65529,16},{65530,16},{65531,16},{65532,16},{65533,16},{65534,16},{0,0},{0,0},{0,0},{0,0},{0,0}
   };
   static const unsigned short UVAC_HT[256][2] = {
      {0,2},{1,2},{4,3},{10,4},{24,5},{25,5},{56,6},{120,7},{500,9},{1014,10},{4084,12},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {11,4},{57,6},{246,8},{501,9},{2038,11},{4085,12},{65416,16},{65417,16},{65418,16},{65419,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {26,5},{247,8},{1015,10},{4086,12},{32706,15},{65420,16},{65421,16},{65422,16},{65423,16},{65424,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {27,5},{248,8},{1016,10},{4087,12},{65425,16},{65426,16},{65427,16},{65428,16},{65429,16},{65430,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {58,6},{502,9},{65431,16},{65432,16},{65433,16},{65434,16},{65435,16},{65436,16},{65437,16},{65438,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {59,6},{1017,10},{65439,16},{65440,16},{65441,16},{65442,16},{65443,16},{65444,16},{65445,16},{65446,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {121,7},{2039,11},{65447,16},{65448,16},{65449,16},{65450,16},{65451,16},{65452,16},{65453,16},{65454,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {122,7},{2040,11},{65455,16},{65456,16},{65457,16},{65458,16},{65459,16},{65460,16},{65461,16},{65462,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {249,8},{65463,16},{65464,16},{65465,16},{65466,16},{65467,16},{65468,16},{65469,16},{65470,16},{65471,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {503,9},{65472,16},{65473,16},{65474,16},{65475,16},{65476,16},{65477,16},{65478,16},{65479,16},{65480,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {504,9},{65481,16},{65482,16},{65483,16},{65484,16},{65485,16},{65486,16},{65487,16},{65488,16},{65489,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {505,9},{65490,16},{65491,16},{65492,16},{65493,16},{65494,16},{65495,16},{65496,16},{65497,16},{65498,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {506,9},{65499,16},{65500,16},{65501,16},{65502,16},{65503,16},{65504,16},{65505,16},{65506,16},{65507,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2041,11},{65508,16},{65509,16},{65510,16},{65511,16},{65512,16},{65513,16},{65514,16},{65515,16},{65516,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {16352,14},{65517,16},{65518,16},{65519,16},{65520,16},{65521,16},{65522,16},{65523,16},{65524,16},{65525,16},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1018,10},{32707,15},{65526,16},{65527,16},{65528,16},{65529,16},{65530,16},{65531,16},{65532,16},{65533,16},{65534,16},{0,0},{0,0},{0,0},{0,0},{0,0}
   };
   static const int YQT[] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,
                             37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
   static const int UVQT[] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
                              99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
   static const float aasf[] = { 1.0f * 2.828427125f, 1.387039845f * 2.828427125f, 1.306562965f * 2.828427125f, 1.175875602f * 2.828427125f,
                                 1.0f * 2.828427125f, 0.785694958f * 2.828427125f, 0.541196100f * 2.828427125f, 0.275899379f * 2.828427125f };

   int row, col, i, k, subsample;
   float fdtbl_Y[64], fdtbl_UV[64];
   unsigned char YTable[64], UVTable[64];

   if(!data || !width || !height || comp > 4 || comp < 1) {
      return 0;
   }

   quality = quality ? quality : 90;
   subsample = quality <= 90 ? 1 : 0;
   quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
   quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

   for(i = 0; i < 64; ++i) {
      int uvti, yti = (YQT[i]*quality+50)/100;
      YTable[stbiw__jpg_ZigZag[i]] = (unsigned char) (yti < 1 ? 1 : yti > 255 ? 255 : yti);
      uvti = (UVQT[i]*quality+50)/100;
      UVTable[stbiw__jpg_ZigZag[i]] = (unsigned char) (uvti < 1 ? 1 : uvti > 255 ? 255 : uvti);
   }

   for(row = 0, k = 0; row < 8; ++row) {
      for(col = 0; col < 8; ++col, ++k) {
         fdtbl_Y[k]  = 1 / (YTable [stbiw__jpg_ZigZag[k]] * aasf[row] * aasf[col]);
         fdtbl_UV[k] = 1 / (UVTable[stbiw__jpg_ZigZag[k]] * aasf[row] * aasf[col]);
      }
   }

   // Write Headers
   {
      static const unsigned char head0[] = { 0xFF,0xD8,0xFF,0xE0,0,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0,0xFF,0xDB,0,0x84,0 };
      static const unsigned char head2[] = { 0xFF,0xDA,0,0xC,3,1,0,2,0x11,3,0x11,0,0x3F,0 };
      const unsigned char head1[] = { 0xFF,0xC0,0,0x11,8,(unsigned char)(height>>8),STBIW_UCHAR(height),(unsigned char)(width>>8),STBIW_UCHAR(width),
                                      3,1,(unsigned char)(subsample?0x22:0x11),0,2,0x11,1,3,0x11,1,0xFF,0xC4,0x01,0xA2,0 };
      s->func(s->context, (void*)head0, sizeof(head0));
      s->func(s->context, (void*)YTable, sizeof(YTable));
      stbiw__putc(s, 1);
      s->func(s->context, UVTable, sizeof(UVTable));
      s->func(s->context, (void*)head1, sizeof(head1));
      s->func(s->context, (void*)(std_dc_luminance_nrcodes+1), sizeof(std_dc_luminance_nrcodes)-1);
      s->func(s->context, (void*)std_dc_luminance_values, sizeof(std_dc_luminance_values));
      stbiw__putc(s, 0x10); // HTYACinfo
      s->func(s->context, (void*)(std_ac_luminance_nrcodes+1), sizeof(std_ac_luminance_nrcodes)-1);
      s->func(s->context, (void*)std_ac_luminance_values, sizeof(std_ac_luminance_values));
      stbiw__putc(s, 1); // HTUDCinfo
      s->func(s->context, (void*)(std_dc_chrominance_nrcodes+1), sizeof(std_dc_chrominance_nrcodes)-1);
      s->func(s->context, (void*)std_dc_chrominance_values, sizeof(std_dc_chrominance_values));
      stbiw__putc(s, 0x11); // HTUACinfo
      s->func(s->context, (void*)(std_ac_chrominance_nrcodes+1), sizeof(std_ac_chrominance_nrcodes)-1);
      s->func(s->context, (void*)std_ac_chrominance_values, sizeof(std_ac_chrominance_values));
      s->func(s->context, (void*)head2, sizeof(head2));
   }

   // Encode 8x8 macroblocks
   {
      int DCY=0, DCU=0, DCV=0;
      stbiw__jpg_bw bw;
      bw.s = s; bw.acc = 0; bw.nbits = 0; bw.len = 0;
      // comp == 2 is grey+alpha (alpha is ignored)
      int ofsG = comp > 2 ? 1 : 0, ofsB = comp > 2 ? 2 : 0;
      const unsigned char *dataR = (const unsigned char *)data;
      const unsigned char *dataG = dataR + ofsG;
      const unsigned char *dataB = dataR + ofsB;
      int x, y, pos;
      if(subsample) {
         for(y = 0; y < height; y += 16) {
            for(x = 0; x < width; x += 16) {
               float Y[256], U[256], V[256];
               for(row = y, pos = 0; row < y+16; ++row) {
                  // row >= height => use last input row
                  int clamped_row = (row < height) ? row : height - 1;
                  int base_p = (stbi__flip_vertically_on_write ? (height-1-clamped_row) : clamped_row)*width*comp;
                  if(x + 16 <= width) {
                     // libstb patch: a 16-pixel run inside the image needs no per-pixel column clamp
                     stbiw__jpg_convert_run(Y+pos, U+pos, V+pos, dataR + base_p + x*comp, ofsG, ofsB, comp, 16);
                     pos += 16;
                  } else
                  for(col = x; col < x+16; ++col, ++pos) {
                     // if col >= width => use pixel from last input column
                     int p = base_p + ((col < width) ? col : (width-1))*comp;
                     float r = dataR[p], g = dataG[p], b = dataB[p];
                     Y[pos]= +0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
                     U[pos]= -0.16874f*r - 0.33126f*g + 0.50000f*b;
                     V[pos]= +0.50000f*r - 0.41869f*g - 0.08131f*b;
                  }
               }
               DCY = stbiw__jpg_processDU(&bw, Y+0,   16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
               DCY = stbiw__jpg_processDU(&bw, Y+8,   16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
               DCY = stbiw__jpg_processDU(&bw, Y+128, 16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
               DCY = stbiw__jpg_processDU(&bw, Y+136, 16, fdtbl_Y, DCY, YDC_HT, YAC_HT);

               // subsample U,V
               {
                  float subU[64], subV[64];
                  int yy, xx;
                  for(yy = 0, pos = 0; yy < 8; ++yy) {
                     for(xx = 0; xx < 8; ++xx, ++pos) {
                        int j = yy*32+xx*2;
                        subU[pos] = (U[j+0] + U[j+1] + U[j+16] + U[j+17]) * 0.25f;
                        subV[pos] = (V[j+0] + V[j+1] + V[j+16] + V[j+17]) * 0.25f;
                     }
                  }
                  DCU = stbiw__jpg_processDU(&bw, subU, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
                  DCV = stbiw__jpg_processDU(&bw, subV, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
               }
            }
         }
      } else {
         for(y = 0; y < height; y += 8) {
            for(x = 0; x < width; x += 8) {
               float Y[64], U[64], V[64];
               for(row = y, pos = 0; row < y+8; ++row) {
                  // row >= height => use last input row
                  int clamped_row = (row < height) ? row : height - 1;
                  int base_p = (stbi__flip_vertically_on_write ? (height-1-clamped_row) : clamped_row)*width*comp;
                  if(x + 8 <= width) {
                     // libstb patch: an 8-pixel run inside the image needs no per-pixel column clamp
                     stbiw__jpg_convert_run(Y+pos, U+pos, V+pos, dataR + base_p + x*comp, ofsG, ofsB, comp, 8);
                     pos += 8;
                  } else
                  for(col = x; col < x+8; ++col, ++pos) {
                     // if col >= width => use pixel from last input column
                     int p = base_p + ((col < width) ? col : (width-1))*comp;
                     float r = dataR[p], g = dataG[p], b = dataB[p];
                     Y[pos]= +0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
                     U[pos]= -0.16874f*r - 0.33126f*g + 0.50000f*b;
                     V[pos]= +0.50000f*r - 0.41869f*g - 0.08131f*b;
                  }
               }

               DCY = stbiw__jpg_processDU(&bw, Y, 8, fdtbl_Y,  DCY, YDC_HT, YAC_HT);
               DCU = stbiw__jpg_processDU(&bw, U, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
               DCV = stbiw__jpg_processDU(&bw, V, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
            }
         }
      }

      // Do the bit alignment of the EOI marker
      stbiw__jpg_bw_finish(&bw);
   }

   // EOI
   stbiw__putc(s, 0xFF);
   stbiw__putc(s, 0xD9);

   return 1;
}

STBIWDEF int stbi_write_jpg_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data, int quality)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_jpg_core(&s, x, y, comp, (void *) data, quality);
}


#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_jpg(char const *filename, int x, int y, int comp, const void *data, int quality)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_jpg_core(&s, x, y, comp, data, quality);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif

#endif // STB_IMAGE_WRITE_IMPLEMENTATION

/* Revision history
      1.16  (2021-07-11)
             make Deflate code emit uncompressed blocks when it would otherwise expand
             support writing BMPs with alpha channel
      1.15  (2020-07-13) unknown
      1.14  (2020-02-02) updated JPEG writer to downsample chroma channels
      1.13
      1.12
      1.11  (2019-08-11)

      1.10  (2019-02-07)
             support utf8 filenames in Windows; fix warnings and platform ifdefs
      1.09  (2018-02-11)
             fix typo in zlib quality API, improve STB_I_W_STATIC in C++
      1.08  (2018-01-29)
             add stbi__flip_vertically_on_write, external zlib, zlib quality, choose PNG filter
      1.07  (2017-07-24)
             doc fix
      1.06 (2017-07-23)
             writing JPEG (using Jon Olick's code)
      1.05   ???
      1.04 (2017-03-03)
             monochrome BMP expansion
      1.03   ???
      1.02 (2016-04-02)
             avoid allocating large structures on the stack
      1.01 (2016-01-16)
             STBIW_REALLOC_SIZED: support allocators with no realloc support
             avoid race-condition in crc initialization
             minor compile issues
      1.00 (2015-09-14)
             installable file IO function
      0.99 (2015-09-13)
             warning fixes; TGA rle support
      0.98 (2015-04-08)
             added STBIW_MALLOC, STBIW_ASSERT etc
      0.97 (2015-01-18)
             fixed HDR asserts, rewrote HDR rle logic
      0.96 (2015-01-17)
             add HDR output
             fix monochrome BMP
      0.95 (2014-08-17)
             add monochrome TGA output
      0.94 (2014-05-31)
             rename private functions to avoid conflicts with stb_image.h
      0.93 (2014-05-27)
             warning fixes
      0.92 (2010-08-01)
             casts to unsigned char to fix warnings
      0.91 (2010-07-17)
             first public release
      0.90   first internal release
*/

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
