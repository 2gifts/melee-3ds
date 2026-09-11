#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MP_DISC_ROOT "build/file-io-fixture/"
#define MP_VISUAL_ROOT "build/file-io-fixture/visuals/"
#include "../port/3ds/file_io.c"
static unsigned char data[90000];
static unsigned pattern(unsigned file,unsigned offset){return (offset*17+(offset>>8)*13+file*79)&255;}
static void check(unsigned file,unsigned offset,unsigned length){
    char name[32];snprintf(name,sizeof(name),"asset-%u.bin",file);
    int id=mp_native_file_id(name);assert(id>=0&&mp_native_file_size(id)==200003);
    memset(data,0xa5,sizeof(data));assert(mp_native_file_read(id,data,length,offset)==(int)length);
    for(unsigned i=0;i<length;++i)assert(data[i]==(offset+i<200003?pattern(file,offset+i):0));
    if(length<sizeof(data))assert(data[length]==0xa5);
}
int main(void){
    /* Sequential, backward, unaligned and buffer-crossing reads of the
     * same open stream must match bytes independently generated on disk. */
    for(unsigned offset=0;offset<196608;offset+=32768)check(0,offset,32768);
    check(0,13,80000);check(0,199987,80);check(0,200003,16);
    FILE *same=NULL;for(unsigned i=0;i<READERS;++i)if(readers[i].stream)same=readers[i].stream;
    check(0,0,32);unsigned open=0;for(unsigned i=0;i<READERS;++i)if(readers[i].stream){++open;assert(readers[i].stream==same);}assert(open==1);
    unsigned random=0x44564452;
    for(unsigned i=0;i<1000;++i){random=random*1664525+1013904223;check((random>>24)%20,random%190000,1+(random>>12)%10000);}
    open=0;for(unsigned i=0;i<READERS;++i)open+=readers[i].stream!=NULL;assert(open==READERS);
    int id=mp_native_file_id("asset-0.bin");
    assert(mp_native_file_read(id,data,16,200004)==-1);
    assert(mp_native_file_read(-1,data,1,0)==-1&&mp_native_file_read(3000,data,1,0)==-1);
    assert(mp_native_file_read(id,NULL,1,0)==-1&&mp_native_file_read(id,NULL,0,0)==0);
    assert(mp_native_file_read(id,data,UINT_MAX,0)==-1);
    const char *invalid[]={"",NULL,"../asset-0.bin","/asset-0.bin","C:/asset-0.bin","a\\asset-0.bin","missing.bin"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i)assert(mp_native_file_id(invalid[i])==-1);
    char long_name[600];memset(long_name,'a',599);long_name[599]=0;assert(mp_native_file_id(long_name)==-1);
    mp_native_files_exit();assert(!file_count);
    for(unsigned i=0;i<READERS;++i)assert(!readers[i].stream);
    check(0,12345,4567);mp_native_files_exit();
    assert(mp_native_cache_menus(200002)==0);
    assert(mp_native_cache_menus(200003)==200003&&menu_cache_count==1);
    assert(mp_native_cache_menus(200003)==200003&&menu_cache_count==1);
    assert(mp_native_cache_menus(UINT_MAX)==1800027&&menu_cache_count==9);
    const char *menus[]={"GmTtAll.usd","MnMaAll.usd","MnSlChr.usd","MnSlMap.usd","MnExtAll.usd",
        "audio/us/nr_title.ssm","audio/us/nr_select.ssm","audio/us/nr_name.ssm","audio/us/nr_vs.ssm"};
    unsigned reads=mp_file_sd_reads,hits=mp_file_cache_hits;
    for(unsigned file=0;file<9;++file){
        int cached_id=mp_native_file_id(menus[file]);
        for(unsigned offset=13;offset<200003;offset+=49997){
            memset(data,0xa5,sizeof(data));assert(mp_native_file_read(cached_id,data,32,offset)==32);
            for(unsigned i=0;i<32;++i)assert(data[i]==(offset+i<200003?pattern(file,offset+i):0));
            assert(data[32]==0xa5);
            /* Archive relocation changes only the destination, never the cached source. */
            memset(data,0xdd,32);
            assert(mp_native_file_read(cached_id,data,32,offset)==32);
            for(unsigned i=0;i<32;++i)assert(data[i]==(offset+i<200003?pattern(file,offset+i):0));
        }
        assert(mp_native_file_read(cached_id,data,32,200003)==32);
        for(unsigned i=0;i<32;++i)assert(data[i]==0);
        assert(mp_native_file_read(cached_id,data,32,200004)==-1);
    }
    assert(mp_file_sd_reads==reads&&mp_file_cache_hits>hits);
    mp_native_files_exit();assert(menu_cache_count==0&&menu_cache_bytes==0);
    assert(mp_native_cache_menus(0)==0);
    int uncached_id=mp_native_file_id(menus[0]);assert(mp_native_file_read(uncached_id,data,32,13)==32);
    assert(mp_file_sd_reads==reads+1);mp_native_files_exit();
    id=mp_native_file_id("GrIz.dat");assert(id>=0&&mp_native_file_size(id)==492294);
    assert(mp_native_file_read(id,data,32,4)==32);for(unsigned i=0;i<32;++i)assert(data[i]=='V');
    for(unsigned i=0;i<20;++i)check(i,99,32); /* Force the visual stream out of the reader cache. */
    assert(mp_native_file_read(id,data,32,4)==32);for(unsigned i=0;i<32;++i)assert(data[i]=='V');
    mp_native_files_exit();
    FILE *bad=fopen(MP_VISUAL_ROOT "GrIz.dat","wb");assert(bad);fputs("bad",bad);fclose(bad);
    id=mp_native_file_id("GrIz.dat");assert(mp_native_file_size(id)==17);
    assert(mp_native_file_read(id,data,17,0)==17&&!memcmp(data,"original fountain",17));mp_native_files_exit();
    assert(remove(MP_VISUAL_ROOT "GrIz.dat")==0);
    id=mp_native_file_id("GrIz.dat");assert(mp_native_file_size(id)==17);mp_native_files_exit();
    const char *stages[]={"GrSt.dat","GrOp.dat","GrNBa.dat","GrNLa.dat"};
    const unsigned stage_sizes[]={248938,118854,67235,660692};
    for(unsigned stage=0;stage<4;++stage){
        id=mp_native_file_id(stages[stage]);assert(mp_native_file_size(id)==stage_sizes[stage]);
        assert(mp_native_file_read(id,data,32,4)==32);for(unsigned i=0;i<32;++i)assert(data[i]=='W');
        for(unsigned i=0;i<20;++i)check(i,99,32);
        assert(mp_native_file_read(id,data,32,4)==32);for(unsigned i=0;i<32;++i)assert(data[i]=='W');
        mp_native_files_exit();char path[160];snprintf(path,sizeof(path),"%s%s",MP_VISUAL_ROOT,stages[stage]);
        FILE *bad_stage=fopen(path,"r+b");assert(bad_stage);fputc(0xff,bad_stage);fclose(bad_stage);
        id=mp_native_file_id(stages[stage]);assert(mp_native_file_size(id)==14);mp_native_files_exit();
        assert(remove(path)==0);id=mp_native_file_id(stages[stage]);assert(mp_native_file_size(id)==14);mp_native_files_exit();
    }
    puts("Native disc reader: sequential/seek/EOF data, 1000 cache-evicting reads, path/bounds checks and close/reopen passed");
    puts("Menu cache: budget limits, immutable copies, EOF padding, zero SD reads on cache hits, cleanup/fallback passed");
    puts("Visual archive: resolved size/path, reader eviction, invalid-file and absent-file fallback passed");
}
