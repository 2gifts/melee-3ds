/* Offline authoring only. Never compiled into a release executable. The
 * exporter retains actual per-draw matrices and original GameCube vertices;
 * host tooling assembles a separate HOME Menu scene from the user's assets. */
#if !defined(MP_SMOKE_TEST)
#error Banner capture is restricted to development builds
#endif
volatile unsigned mp_banner_capture;
unsigned mp_banner_capture_number;
static FILE*banner_file;
static unsigned banner_active;
static void banner_capture_begin(void){banner_active=mp_banner_capture!=0;}
static unsigned banner_bytes,banner_textures[1024][3],banner_texture_count;
static void banner_write(const void*data,unsigned bytes){
    if(bytes>32*1024*1024-banner_bytes||fwrite(data,1,bytes,banner_file)!=bytes)
        mp_native_panic("Banner export exceeds limit or SD write failed");
    banner_bytes+=bytes;
}
static void banner_capture_draw(const Vertex*be,unsigned count,const Draw*d){
    if(!banner_active)return;
    if(!banner_file){char path[120];snprintf(path,sizeof(path),"sdmc:/3ds/melee/banner-%04u.bin",mp_banner_capture_number);
        banner_file=fopen(path,"wb");if(!banner_file)mp_native_panic("Cannot create banner export");
        banner_bytes=banner_texture_count=0;banner_write("MPBN0001",8);
    }
    if(!d->gpu||d->points||d->cull==3||count>65535||!d->index_count)return;
    unsigned header[4]={0x57415244,count,d->index_count,sizeof(MPGPUUniforms)};
    banner_write(header,sizeof(header));banner_write(d,sizeof(*d));
    banner_write((void*)d->gpu,sizeof(MPGPUUniforms));banner_write(be,count*sizeof(*be));
    banner_write((void*)d->indices,d->index_count*2);
    unsigned texture=0;
    if(d->image&&d->w&&d->h){
        for(unsigned i=0;i<banner_texture_count;++i)if(banner_textures[i][0]==d->image&&banner_textures[i][1]==d->palette&&banner_textures[i][2]==d->format)texture=i+1;
        if(!texture){
            if(banner_texture_count==1024||d->w>1024||d->h>1024)mp_native_panic("Banner texture limit exceeded");
            texture=++banner_texture_count;banner_textures[texture-1][0]=d->image;banner_textures[texture-1][1]=d->palette;banner_textures[texture-1][2]=d->format;
            unsigned marker=texture|0x80000000;banner_write(&marker,4);
            u8*rgba_pixels=malloc(d->w*d->h*4);if(!rgba_pixels)mp_native_panic("Banner texture allocation failed");
            for(unsigned y=0;y<d->h;++y)for(unsigned x=0;x<d->w;++x){u32 c=decode(d,x,y);unsigned p=(y*d->w+x)*4;
                rgba_pixels[p]=c>>24;rgba_pixels[p+1]=c>>16;rgba_pixels[p+2]=c>>8;rgba_pixels[p+3]=c;}
            banner_write(rgba_pixels,d->w*d->h*4);free(rgba_pixels);return;
        }
    }
    banner_write(&texture,4);
}
static void banner_capture_end(void){
    if(banner_file){if(fclose(banner_file))mp_native_panic("Cannot finish banner export");banner_file=NULL;
        ++mp_banner_capture_number;mp_banner_capture=0;banner_active=0;}
}
