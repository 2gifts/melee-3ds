/* The wire values/cache contain BE8 words; C3D setters take native floats.
 * Retain cached values for skipped rows: a later shader path compares against
 * what is actually installed, not against the previous draw's unused inputs. */
#ifdef MP_SMOKE_TEST
volatile unsigned uniform_scope_disable;
unsigned uniform_scope_draws[3],uniform_scope_checks,uniform_scope_writes;
#endif

static void upload_uniform_row(const u32 (*u)[4],unsigned i){
#ifdef MP_AFFINE_IDENTITY_SHADER
    /* Keep the wire state untouched. Compare/cache the actual installed
     * value, including transitions to a reference, unlit or complex draw. */
    if(i==MP_GPU_CONFIG){
        u32 row[4];memcpy(row,u[i],sizeof(row));
        if(affine_identity_current)row[0]=__builtin_bswap32(0xbf800000u);
        ++uniform_scope_checks;
        if(!uniform_valid[i]||memcmp(row,uniform_cache[i],sizeof(row))){
            union{u32 u[4];float f[4];}v;for(unsigned j=0;j<4;++j)v.u[j]=read32(row+j);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,i,v.f[0],v.f[1],v.f[2],v.f[3]);
            memcpy(uniform_cache[i],row,sizeof(row));uniform_valid[i]=1;++uniform_scope_writes;
        }
        return;
    }
#endif
#ifdef MP_SMOKE_TEST
    ++uniform_scope_checks;
#endif
    if(!uniform_valid[i]||u[i][0]!=uniform_cache[i][0]||u[i][1]!=uniform_cache[i][1]||u[i][2]!=uniform_cache[i][2]||u[i][3]!=uniform_cache[i][3]){
        union{u32 u[4];float f[4];}v;for(unsigned j=0;j<4;++j)v.u[j]=read32(&u[i][j]);
        if(i<60)++palette_rows_sent;
        C3D_FVUnifSet(GPU_VERTEX_SHADER,i,v.f[0],v.f[1],v.f[2],v.f[3]);
        memcpy(uniform_cache[i],u[i],16);uniform_valid[i]=1;
#ifdef MP_SMOKE_TEST
        ++uniform_scope_writes;
#endif
    }
}

static void upload_uniform_material(const MPGPUUniforms*gpu){
    const u32*material=(const void*)gpu->material0;
    if(!material0_valid||memcmp(material0_cache,material,16)){
        union{u32 u[4];float f[4];}m;for(unsigned j=0;j<4;++j)m.u[j]=read32(material+j);
        C3D_FixedAttribSet(4,m.f[0],m.f[1],m.f[2],m.f[3]);
        memcpy(material0_cache,material,16);material0_valid=1;
    }
}

#ifdef MP_SMOKE_TEST
/* Previous complete scan remains independently selectable for live A/B. */
static void upload_uniform_reference(const MPGPUUniforms*gpu,unsigned matrix_mask){
    const u32(*u)[4]=(const void*)gpu;
    unsigned rows=read32(&gpu->matrix_rows);
    int lighting=u[MP_GPU_CONFIG][0]!=0||u[MP_GPU_CONFIG+1][0]!=0;
    upload_uniform_material(gpu);
    for(unsigned i=0;i<MP_GPU_UNIFORMS;++i){
        if(i<30&&(i>=rows||!(matrix_mask&(1u<<i)))){++palette_rows_skipped;continue;}
        if(i>=30&&i<60&&(!lighting||i-30>=rows||!(matrix_mask&(1u<<(i-30))))){++palette_rows_skipped;continue;}
        if(i>=MP_GPU_LIGHT_POS&&i<MP_GPU_SHADE){
            if(i<MP_GPU_LIGHT_COLOR||i>=MP_GPU_LIGHT_COLOR+4){if(!lighting||!u[MP_GPU_LIGHT_COLOR+(i-MP_GPU_LIGHT_POS)%4][3])continue;}
        }
        if(i>=MP_GPU_AMBIENT&&i<MP_GPU_AMBIENT+2&&!u[MP_GPU_CONFIG+i-MP_GPU_AMBIENT][0])continue;
        upload_uniform_row(u,i);
    }
}
#endif

static void upload_uniform_palette(const u32(*u)[4],unsigned mask,unsigned base){
    unsigned used=0;
    while(mask){unsigned row=__builtin_ctz(mask);mask&=mask-1;
        upload_uniform_row(u,base+row);++used;
    }
    palette_rows_skipped+=30-used;
}

static void upload_gpu_uniforms(const MPGPUUniforms*gpu,unsigned matrix_mask,unsigned branch){
    const u32(*u)[4]=(const void*)gpu;
    unsigned route=(branch&1)?0:(branch&2)?2:1;
#ifdef MP_SMOKE_TEST
    ++uniform_scope_draws[route];
    if(uniform_scope_disable){upload_uniform_reference(gpu,matrix_mask);return;}
#endif
    unsigned rows=read32(&gpu->matrix_rows);
    unsigned mask=matrix_mask&(rows<30?((1u<<rows)-1):0x3fffffffu);
    upload_uniform_palette(u,mask,MP_GPU_POS);
    int lighting=u[MP_GPU_CONFIG][0]!=0||u[MP_GPU_CONFIG+1][0]!=0;
    if(route==2&&lighting)upload_uniform_palette(u,mask,MP_GPU_NORMAL);
    else palette_rows_skipped+=30;
    for(unsigned i=MP_GPU_PROJECTION;i<MP_GPU_LIGHT_POS;++i)upload_uniform_row(u,i);
    upload_uniform_row(u,MP_GPU_SHADE);
    if(route==0)return; /* flat_vertex reads only model/projection/shade[0]. */
    upload_uniform_material(gpu);
    for(unsigned i=MP_GPU_SHADE+1;i<MP_GPU_AMBIENT;++i)upload_uniform_row(u,i);
    upload_uniform_row(u,MP_GPU_MATERIAL1);upload_uniform_row(u,MP_GPU_CLAMP);
#ifdef MP_AFFINE_IDENTITY_SHADER
    /* The candidate unlit program now reads the identity marker here. */
    if(route==1)upload_uniform_row(u,MP_GPU_CONFIG);
#endif
    if(route==1)return; /* unlit_vertex has no normal/light/config reads. */
    for(unsigned light=0;light<4;++light){
        upload_uniform_row(u,MP_GPU_LIGHT_COLOR+light);
        if(lighting&&u[MP_GPU_LIGHT_COLOR+light][3]){
            upload_uniform_row(u,MP_GPU_LIGHT_POS+light);
            upload_uniform_row(u,MP_GPU_LIGHT_DIR+light);
            upload_uniform_row(u,MP_GPU_ATTENUATION+light);
            upload_uniform_row(u,MP_GPU_COS_ATTENUATION+light);
        }
    }
    for(unsigned channel=0;channel<2;++channel){
        upload_uniform_row(u,MP_GPU_CONFIG+channel);
        if(u[MP_GPU_CONFIG+channel][0])upload_uniform_row(u,MP_GPU_AMBIENT+channel);
    }
}
