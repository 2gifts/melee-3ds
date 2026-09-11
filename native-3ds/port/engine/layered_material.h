#ifndef MP_LAYERED_MATERIAL_H
#define MP_LAYERED_MATERIAL_H
/* A supported GX chain, evaluated per fragment instead of substituting white
 * for both textures: clamp(mix(K, T0, blend) - T1) * lit raster.
 * Alpha is mix(initial register alpha, T1.a, T1.a) * raster alpha.
 * This is used by Peach's animated turnip faces. Match the algebra rather
 * than an item ID, image address or particular face/texture format. */
typedef struct {
    unsigned image,width,height,format,palette,palette_format,palette_count;
    unsigned wrap_s,wrap_t,uv,tint,blend,alpha;
    unsigned mode,base;
} MPTextureLayer;
enum {MP_FRAGMENT_SUBTRACT,MP_FRAGMENT_SHIELD_START,MP_FRAGMENT_TINT};
_Static_assert(sizeof(MPTextureLayer)==15*4,"Fragment material bridge layout");
static inline int mp_layered_material(const unsigned c[16][30],unsigned n){
    static const unsigned rgb[4][4]={{15,15,15,14},{0,8,14,15},{8,15,15,0},{15,0,10,15}};
    static const unsigned alpha[4][4]={{7,7,7,7},{7,7,7,0},{1,4,4,7},{7,0,5,7}};
    if(n!=4||c[1][0]>=8||c[2][0]>=8||c[1][1]>=8||c[2][1]>=8||c[1][1]==c[2][1]||c[3][2]!=4)return 0;
    /* Only scalar konst selectors can provide an interpolation weight. */
    if(c[0][23]>=32||c[1][23]>=32||(c[1][23]>=8&&c[1][23]<16))return 0;
    for(unsigned i=0;i<4;++i){
        for(unsigned j=0;j<4;++j)if(c[i][3+j]!=rgb[i][j]||c[i][7+j]!=alpha[i][j])return 0;
        if(c[i][11]!=(i==2)||c[i][12]||c[i][13]||c[i][14]!=1||c[i][15]||
           c[i][16]||c[i][17]||c[i][18]||c[i][19]!=(i>=2)||c[i][20]||c[i][21]||c[i][22])return 0;
    }
    return 1;
}
#endif
