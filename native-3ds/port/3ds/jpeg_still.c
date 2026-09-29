#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>
/* THP's JPEG variant stores the entropy-coded scan without byte stuffing
 * (the THP decoder reads it as a raw bitstream). Rebuild a standard JPEG:
 * headers through SOS unchanged, 0x00 after every 0xFF in the scan, then the
 * final EOI. Returns the new size, or 0 if the stream has no scan. */
static unsigned thp_to_jpeg(const unsigned char*in,unsigned size,unsigned char**result){
    if(size<4||in[0]!=0xff||in[1]!=0xd8)return 0;
    unsigned i=2,scan=0;
    while(i+4<=size&&in[i]==0xff){
        unsigned marker=in[i+1],length=(in[i+2]<<8)|in[i+3];
        if(length<2||i+2+length>size)return 0;
        i+=2+length;
        if(marker==0xda){scan=i;break;}
    }
    if(!scan)return 0;
    unsigned end=size;
    while(end>=scan+2&&!(in[end-2]==0xff&&in[end-1]==0xd9))--end;
    end=end>=scan+2?end-2:size;
    unsigned stuffed=0;for(unsigned k=scan;k<end;++k)stuffed+=in[k]==0xff;
    unsigned char*out=malloc(scan+(end-scan)+stuffed+2);if(!out)return 0;
    memcpy(out,in,scan);unsigned n=scan;
    for(unsigned k=scan;k<end;++k){out[n++]=in[k];if(in[k]==0xff)out[n++]=0;}
    out[n++]=0xff;out[n++]=0xd9;*result=out;return n;
}
/* The Classic/Adventure/All-Star congratulations art (GmRegend*.thp) is one
 * THP-coded JPEG. GameCube code decodes it with the THP library's
 * paired-single IDCT into Y/U/V planes; here it becomes one GX RGB565
 * texture (4x4 texel tiles, big-endian texels) of width x height, packed in
 * dims as width<<16|height. The image is clamped at its edges if the file is
 * smaller. Returns 0 if the file cannot be decoded. */
unsigned mp_native_jpeg_rgb565(const void*data,unsigned size,unsigned dims,void*output){
    unsigned width=dims>>16,height=dims&0xffff;
    if(!data||!size||!width||!height||!output)return 0;
    unsigned char*jpeg=NULL;unsigned jpeg_size=thp_to_jpeg(data,size,&jpeg);if(!jpeg_size)return 0;
    tjhandle tj=tjInitDecompress();if(!tj){free(jpeg);return 0;}
    int w=0,h=0,subsampling,colorspace;unsigned ok=0;unsigned char*rgb=NULL;
    if(!tjDecompressHeader3(tj,jpeg,jpeg_size,&w,&h,&subsampling,&colorspace)&&w>0&&h>0&&
       (rgb=malloc((size_t)w*h*3))&&!tjDecompress2(tj,jpeg,jpeg_size,rgb,w,0,h,TJPF_RGB,0)){
        u8*out=output;unsigned tiles_x=(width+3)/4;
        for(unsigned y=0;y<height;++y){
            const unsigned char*row=rgb+(size_t)(y<(unsigned)h?y:(unsigned)h-1)*w*3;
            for(unsigned x=0;x<width;++x){
                const unsigned char*p=row+(x<(unsigned)w?x:(unsigned)w-1)*3;
                unsigned v=((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3);
                u8*t=out+(((y/4)*tiles_x+x/4)*16+(y%4)*4+x%4)*2;t[0]=v>>8;t[1]=v;
            }
        }
        ok=1;
    }
    free(rgb);free(jpeg);tjDestroy(tj);return ok;
}
