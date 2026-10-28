#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// *************************************************************************************************
// Softimage PIC loader
// by Tom Seddon
//
// See http://softimage.wiki.softimage.com/index.php/INFO:_PIC_file_format
// See http://ozviz.wasp.uwa.edu.au/~pbourke/dataformats/softimagepic/

static int pic_is4(stream *s,const char *str)
{
   int i;
   for (i=0; i<4; ++i)
      if (s->get8() != (byte)str[i])
         return 0;

   return 1;
}

static int pic_test_core(stream *s)
{
   int i;

   if (!pic_is4(s,"\x53\x80\xF6\x34"))
      return 0;

   for(i=0;i<84;++i)
      s->get8();

   if (!pic_is4(s,"PICT"))
      return 0;

   return 1;
}

typedef struct
{
   byte size,type,channel;
} pic_packet;

static byte *readval(stream *s, int channel, byte *dest)
{
   int mask=0x80, i;

   for (i=0; i<4; ++i, mask>>=1) {
      if (channel & mask) {
         if (s->at_eof()) return fail_bytes("bad file","PIC file too short");
         dest[i]=s->get8();
      }
   }

   return dest;
}

static void copyval(int channel,byte *dest,const byte *src)
{
   int mask=0x80,i;

   for (i=0;i<4; ++i, mask>>=1)
      if (channel&mask)
         dest[i]=src[i];
}

static byte *pic_load_core(stream *s,int width,int height,int *comp, byte *result)
{
   int act_comp=0,num_packets=0,y,chained;
   pic_packet packets[10];

   // this will (should...) cater for even some bizarre stuff like having data
    // for the same channel in multiple packets.
   do {
      pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return fail_bytes("bad format","too many packets");

      packet = &packets[num_packets++];

      chained = s->get8();
      packet->size    = s->get8();
      packet->type    = s->get8();
      packet->channel = s->get8();

      act_comp |= packet->channel;

      if (s->at_eof())          return fail_bytes("bad file","file too short (reading packets)");
      if (packet->size != 8)  return fail_bytes("bad format","packet isn't 8bpp");
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3); // has alpha channel?

   for(y=0; y<height; ++y) {
      int packet_idx;

      for(packet_idx=0; packet_idx < num_packets; ++packet_idx) {
         pic_packet *packet = &packets[packet_idx];
         byte *dest = result+y*width*4;

         switch (packet->type) {
            default:
               return fail_bytes("bad format","packet has bad compression type");

            case 0: {//uncompressed
               int x;

               for(x=0;x<width;++x, dest+=4)
                  if (!readval(s,packet->channel,dest))
                     return 0;
               break;
            }

            case 1://Pure RLE
               {
                  int left=width, i;

                  while (left>0) {
                     byte count,value[4];

                     count=s->get8();
                     if (s->at_eof())   return fail_bytes("bad file","file too short (pure read count)");

                     if (count > left)
                        count = (byte) left;

                     if (!readval(s,packet->channel,value))  return 0;

                     for(i=0; i<count; ++i,dest+=4)
                        copyval(packet->channel,dest,value);
                     left -= count;
                  }
               }
               break;

            case 2: {//Mixed RLE
               int left=width;
               while (left>0) {
                  int count = s->get8(), i;
                  if (s->at_eof())  return fail_bytes("bad file","file too short (mixed read count)");

                  if (count >= 128) { // Repeated
                     byte value[4];

                     if (count==128)
                        count = s->get16be();
                     else
                        count -= 127;
                     if (count > left)
                        return fail_bytes("bad file","scanline overrun");

                     if (!readval(s,packet->channel,value))
                        return 0;

                     for(i=0;i<count;++i, dest += 4)
                        copyval(packet->channel,dest,value);
                  } else { // Raw
                     ++count;
                     if (count>left) return fail_bytes("bad file","scanline overrun");

                     for(i=0;i<count;++i, dest+=4)
                        if (!readval(s,packet->channel,dest))
                           return 0;
                  }
                  left-=count;
               }
               break;
            }
         }
      }
   }

   return result;
}

void * pic::load(stream *s, int *px, int *py, int *comp, int req_comp, result_info *ri)
{
   byte *result;
   int i, x,y, internal_comp;
   (void)(ri);

   if (!comp) comp = &internal_comp;

   for (i=0; i<92; ++i)
      s->get8();

   x = s->get16be();
   y = s->get16be();

   if (y > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");
   if (x > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");

   if (s->at_eof())  return fail_bytes("bad file","file too short (pic header)");
   if (!mad3sizes_valid(x, y, 4, 0)) return fail_bytes("too large", "PIC image too large to decode");

   s->get32be(); //skip `ratio'
   s->get16be(); //skip `fields'
   s->get16be(); //skip `pad'

   // intermediate buffer is RGBA
   result = (byte *) alloc_mad3(x, y, 4, 0);
   if (!result) return fail_bytes("outofmem", "Out of memory");
   memset(result, 0xff, x*y*4);

   if (!pic_load_core(s,x,y,comp, result)) {
      std::free(result);
      result=0;
   }
   *px = x;
   *py = y;
   if (req_comp == 0) req_comp = *comp;
   result=convert_format(result,4,req_comp,x,y);

   return result;
}

int pic::test(stream *s)
{
   int r = pic_test_core(s);
   s->rewind();
   return r;
}

int pic::info(stream *s, int *x, int *y, int *comp)
{
   int act_comp=0,num_packets=0,chained,dummy;
   pic_packet packets[10];

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (!pic_is4(s,"\x53\x80\xF6\x34")) {
      s->rewind();
      return 0;
   }

   s->skip(88);

   *x = s->get16be();
   *y = s->get16be();
   if (s->at_eof()) {
      s->rewind();
      return 0;
   }
   if ( (*x) != 0 && (1 << 28) / (*x) < (*y)) {
      s->rewind();
      return 0;
   }

   s->skip(8);

   do {
      pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return 0;

      packet = &packets[num_packets++];
      chained = s->get8();
      packet->size    = s->get8();
      packet->type    = s->get8();
      packet->channel = s->get8();
      act_comp |= packet->channel;

      if (s->at_eof()) {
          s->rewind();
          return 0;
      }
      if (packet->size != 8) {
          s->rewind();
          return 0;
      }
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3);

   return 1;
}

}  // namespace stb::codec::detail
