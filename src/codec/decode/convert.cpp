#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


//////////////////////////////////////////////////////////////////////////////
//
//  generic converter from built-in img_n to req_comp
//    individual types do this automatically as much as possible (e.g. jpeg
//    does all cases internally since it needs to colorspace convert anyway,
//    and it never has alpha, so very few cases ). png can automatically
//    interleave an alpha=255 channel, but falls back to this for other cases
//
//  assume data buffer is malloced, so malloc a new one and free that one
//  only failure mode is malloc failing

byte compute_y(int r, int g, int b)
{
   return (byte) (((r*77) + (g*150) +  (29*b)) >> 8);
}

unsigned char *convert_format(unsigned char *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   unsigned char *good;

   if (req_comp == img_n) return data;
   assert(req_comp >= 1 && req_comp <= 4);

   good = (unsigned char *) alloc_mad3(req_comp, x, y, 0);
   if (good == NULL) {
      std::free(data);
      return fail_bytes("outofmem", "Out of memory");
   }

   for (j=0; j < (int) y; ++j) {
      unsigned char *src  = data + j * x * img_n   ;
      unsigned char *dest = good + j * x * req_comp;

      #define COMBO(a,b)  ((a)*8+(b))
      #define CASE(a,b)   case COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (COMBO(img_n, req_comp)) {
         CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
         CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
         CASE(2,1) { dest[0]=src[0];                                                  } break;
         CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
         CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
         CASE(3,1) { dest[0]=compute_y(src[0],src[1],src[2]);                   } break;
         CASE(3,2) { dest[0]=compute_y(src[0],src[1],src[2]); dest[1] = 255;    } break;
         CASE(4,1) { dest[0]=compute_y(src[0],src[1],src[2]);                   } break;
         CASE(4,2) { dest[0]=compute_y(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;
         default: assert(0); std::free(data); std::free(good); return fail_bytes("unsupported", "Unsupported format conversion");
      }
      #undef CASE
   }

   std::free(data);
   return good;
}

static std::uint16_t compute_y_16(int r, int g, int b)
{
   return (std::uint16_t) (((r*77) + (g*150) +  (29*b)) >> 8);
}

std::uint16_t *convert_format16(std::uint16_t *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   std::uint16_t *good;

   if (req_comp == img_n) return data;
   assert(req_comp >= 1 && req_comp <= 4);

   good = (std::uint16_t *) alloc(req_comp * x * y * 2);
   if (good == NULL) {
      std::free(data);
      return (std::uint16_t *) fail_bytes("outofmem", "Out of memory");
   }

   for (j=0; j < (int) y; ++j) {
      std::uint16_t *src  = data + j * x * img_n   ;
      std::uint16_t *dest = good + j * x * req_comp;

      #define COMBO(a,b)  ((a)*8+(b))
      #define CASE(a,b)   case COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (COMBO(img_n, req_comp)) {
         CASE(1,2) { dest[0]=src[0]; dest[1]=0xffff;                                     } break;
         CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
         CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=0xffff;                     } break;
         CASE(2,1) { dest[0]=src[0];                                                     } break;
         CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
         CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                     } break;
         CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=0xffff;        } break;
         CASE(3,1) { dest[0]=compute_y_16(src[0],src[1],src[2]);                   } break;
         CASE(3,2) { dest[0]=compute_y_16(src[0],src[1],src[2]); dest[1] = 0xffff; } break;
         CASE(4,1) { dest[0]=compute_y_16(src[0],src[1],src[2]);                   } break;
         CASE(4,2) { dest[0]=compute_y_16(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                       } break;
         default: assert(0); std::free(data); std::free(good); return (std::uint16_t*) fail_bytes("unsupported", "Unsupported format conversion");
      }
      #undef CASE
   }

   std::free(data);
   return good;
}

}  // namespace stb::codec::detail
