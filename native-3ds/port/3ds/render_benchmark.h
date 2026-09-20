#ifndef MP_RENDER_BENCHMARK_H
#define MP_RENDER_BENCHMARK_H
/* Development-only frame-boundary benchmark. Requests select renderer flags;
 * no engine state, controller input or simulation cadence is changed here.
 * The host must pause the original match and disconnect during measurement. */
#include "bottom_state.h"
extern MPBottomState mp_bottom_observed;
extern u64 mp_render_command_bytes;
extern unsigned mp_render_command_lists,mp_render_command_errors;
volatile unsigned mp_render_bench_request,mp_render_bench_status;
volatile unsigned mp_render_bench_frames=180;
volatile unsigned mp_render_bench_experiment; /* 0: stereo reuse; 1: early queue; 2: stream queue; 3: async presentation */
static const unsigned render_bench_order[]={0,1,2,2,1,0};
#define RENDER_BENCH_SAMPLES 6
#define RENDER_BENCH_MAX_FRAMES 600
typedef struct {
    u64 tick,wait,commands,early_wait,early_span;
    double gpu_ms;
    unsigned queues,lists,errors,checks,check_ticks,draws[2],vertices[2],shader_vertices[3],early_starts,early_finishes;
#ifdef MP_ASYNC_PRESENTATION
    unsigned deferrals,retirements;
#endif
#ifdef MP_CLAMPED_SHADE_TEST
    unsigned clamped_vertices;
#endif
#ifdef MP_UNLIT_AFFINE_SHADER
    unsigned unlit_draws[2],unlit_vertices[2];
#endif
#ifdef MP_RENDER_WORKER
    u64 worker_wait,worker_busy,worker_copy,source_copied,source_hit_bytes,borrowed_bytes;
    unsigned borrowed_draws;
    unsigned source_hits,source_checks,source_mismatches;
    unsigned worker_jobs,worker_snapshots,worker_fallbacks,worker_flushes,worker_failures;
#endif
} RenderBenchCounters;
typedef struct {
    unsigned mode,start_frame,end_frame,count;
    RenderBenchCounters start,end;
    u64 intervals[RENDER_BENCH_MAX_FRAMES];
    u64 rendered_vertices,rendered_draws;
} RenderBenchSample;
static RenderBenchSample render_bench_samples[RENDER_BENCH_SAMPLES];
static unsigned render_bench_id,render_bench_index,render_bench_warm,render_bench_target;
static unsigned render_bench_saved[16],render_bench_experiment;
#ifdef MP_RENDER_WORKER
extern volatile unsigned mp_render_worker_disable,mp_render_worker_async;
extern volatile unsigned mp_render_worker_source_cache_disable,mp_render_worker_source_cache_validate;
extern u64 mp_render_worker_source_copied_bytes,mp_render_worker_source_hit_bytes;
extern unsigned mp_render_worker_source_hits,mp_render_worker_source_checks,mp_render_worker_source_mismatches;
extern unsigned mp_render_worker_borrowed_draws;
extern u64 mp_render_worker_borrowed_bytes;
extern u64 mp_render_worker_wait_ticks,mp_render_worker_busy_ticks,mp_render_worker_copy_ticks;
extern unsigned mp_render_worker_jobs,mp_render_worker_snapshots,mp_render_worker_snapshot_fallbacks,mp_render_worker_arena_flushes,mp_render_worker_failures;
#endif
#ifdef MP_RENDER_REWORK_TEST
extern volatile unsigned gpu_clamped_disable,geometry_planes_disable,geometry_early_disable;
volatile unsigned mp_render_bench_rework_mask=7;
static volatile unsigned*const rework_flags[]={&gpu_clamped_disable,&geometry_planes_disable,&geometry_early_disable};
#endif
static unsigned render_bench_mode_at(unsigned i){
#if defined(MP_UNLIT_AFFINE_SHADER) || defined(MP_RENDER_WORKER)
    static const unsigned two_modes[]={0,1,1,0,0,1};
#endif
#ifdef MP_UNLIT_AFFINE_SHADER
    if(render_bench_experiment==5)return two_modes[i];
#endif
#ifdef MP_RENDER_WORKER
    if(render_bench_experiment==6)return two_modes[i];
#endif
#ifdef MP_RENDER_REWORK_TEST
    if(render_bench_experiment==8)return (unsigned[]){0,1,1,0,0,1}[i];
#endif
    return render_bench_order[i];
}
#ifdef MP_CLAMPED_SHADE_TEST
/* These variables belong to the BE8 engine. Native code exchanges words in
 * explicit byte order; it never changes simulation state. */
extern volatile unsigned shade_clamped_disable,shade_clamped_validate;
extern unsigned shade_clamped_vertices;
#endif
static MPBottomState render_bench_scene;
static u64 render_bench_last;

static RenderBenchCounters render_bench_counters(void){
    RenderBenchCounters c={.tick=svcGetSystemTick(),.wait=render_bench_gpu_wait,
        .commands=mp_render_command_bytes,.gpu_ms=render_bench_gpu_ms,
        .queues=render_bench_gpu_queues,.lists=mp_render_command_lists,.errors=mp_render_command_errors,
        .checks=stereo_reuse_checks,.check_ticks=stereo_reuse_check_ticks};
    c.early_wait=mp_early_queue_wait_ticks;c.early_span=mp_early_queue_span_ticks;
    c.early_starts=mp_early_queue_starts;c.early_finishes=mp_early_queue_finishes;
#ifdef MP_ASYNC_PRESENTATION
    c.deferrals=mp_early_queue_deferrals;c.retirements=mp_early_queue_retirements;
#endif
#ifdef MP_CLAMPED_SHADE_TEST
    c.clamped_vertices=__builtin_bswap32(shade_clamped_vertices);
#endif
#ifdef MP_UNLIT_AFFINE_SHADER
    memcpy(c.unlit_draws,unlit_affine_draws,sizeof(c.unlit_draws));memcpy(c.unlit_vertices,unlit_affine_vertices,sizeof(c.unlit_vertices));
#endif
#ifdef MP_RENDER_WORKER
    c.worker_wait=mp_render_worker_wait_ticks;c.worker_busy=mp_render_worker_busy_ticks;c.worker_copy=mp_render_worker_copy_ticks;
    c.worker_jobs=mp_render_worker_jobs;c.worker_snapshots=mp_render_worker_snapshots;
    c.worker_fallbacks=mp_render_worker_snapshot_fallbacks;c.worker_flushes=mp_render_worker_arena_flushes;c.worker_failures=mp_render_worker_failures;
    c.source_copied=mp_render_worker_source_copied_bytes;c.source_hit_bytes=mp_render_worker_source_hit_bytes;
    c.source_hits=mp_render_worker_source_hits;c.source_checks=mp_render_worker_source_checks;c.source_mismatches=mp_render_worker_source_mismatches;
    c.borrowed_draws=mp_render_worker_borrowed_draws;c.borrowed_bytes=mp_render_worker_borrowed_bytes;
#endif
    memcpy(c.draws,stereo_reuse_draws,sizeof(c.draws));memcpy(c.vertices,stereo_reuse_vertices,sizeof(c.vertices));
    memcpy(c.shader_vertices,shader_shortcut_vertices,sizeof(c.shader_vertices));return c;
}
static void render_bench_mode(unsigned mode){
#ifdef MP_RENDER_REWORK_TEST
    if(render_bench_experiment==7||render_bench_experiment==8){
        unsigned enabled=render_bench_experiment==7?(mode==0?0:mode==1?1:7):(mode?7:7&~mp_render_bench_rework_mask);
        for(unsigned i=0;i<3;++i)*rework_flags[i]=__builtin_bswap32(!(enabled&(1u<<i)));
        stereo_reuse_disable=1;gpu_early_queue_disable=0;gpu_stream_queue_disable=0;gpu_early_queue_bytes=32768;
        shade_clamped_disable=0;shade_clamped_validate=0;gpu_async_present_disable=0;
        mp_render_worker_source_cache_disable=0;mp_render_worker_source_cache_validate=0;
        mp_render_worker_async=1;mp_render_worker_disable=0;return;
    }
#endif
#ifdef MP_RENDER_WORKER
    if(render_bench_experiment==6){
        stereo_reuse_disable=1;gpu_early_queue_disable=0;gpu_stream_queue_disable=0;gpu_early_queue_bytes=16384;
#ifdef MP_CLAMPED_SHADE_TEST
        shade_clamped_disable=0;shade_clamped_validate=0;
#endif
#ifdef MP_ASYNC_PRESENTATION
        gpu_async_present_disable=0;
#endif
        mp_render_worker_source_cache_disable=0;mp_render_worker_source_cache_validate=0;
        mp_render_worker_async=1;mp_render_worker_disable=mode==0;return;
    }
#endif
#ifdef MP_UNLIT_AFFINE_SHADER
    if(render_bench_experiment==5){
        stereo_reuse_disable=1;gpu_early_queue_disable=0;gpu_stream_queue_disable=0;gpu_early_queue_bytes=16384;
#ifdef MP_CLAMPED_SHADE_TEST
        shade_clamped_disable=0;shade_clamped_validate=0;
#endif
#ifdef MP_ASYNC_PRESENTATION
        gpu_async_present_disable=0;
#endif
        unlit_affine_disable=mode==0;return;
    }
#endif
#ifdef MP_CLAMPED_SHADE_TEST
    if(render_bench_experiment==4){
        stereo_reuse_disable=1;gpu_early_queue_disable=0;gpu_stream_queue_disable=0;gpu_early_queue_bytes=16384;
        shade_clamped_disable=__builtin_bswap32(mode==0);shade_clamped_validate=0;
#ifdef MP_ASYNC_PRESENTATION
        gpu_async_present_disable=mode!=2;
#endif
        return;
    }
#endif
#ifdef MP_ASYNC_PRESENTATION
    if(render_bench_experiment==3){
        stereo_reuse_disable=1;gpu_early_queue_disable=0;gpu_stream_queue_disable=0;
        gpu_early_queue_bytes=mode==2?32768:16384;
        gpu_async_present_disable=mode==0;return;
    }
    gpu_async_present_disable=1;
#endif
    if(render_bench_experiment==2){
        stereo_reuse_disable=1;gpu_early_queue_disable=0;
        gpu_stream_queue_disable=mode==0;
        gpu_early_queue_bytes=mode==1?16384:32768;return;
    }
    gpu_stream_queue_disable=1;
    if(render_bench_experiment==1){
        stereo_reuse_disable=1;gpu_early_queue_disable=mode==0;
        gpu_early_queue_bytes=mode==1?8192:32768;return;
    }
    gpu_early_queue_disable=1;
    stereo_reuse_disable=mode==0;stereo_reuse_bounds_reference=0;stereo_reuse_shared_disable=mode==1;
}
static void render_bench_restore(void){
#ifdef MP_RENDER_REWORK_TEST
    for(unsigned i=0;i<3;++i)*rework_flags[i]=render_bench_saved[13+i];
#endif
#ifdef MP_RENDER_WORKER
    mp_render_worker_disable=render_bench_saved[9];mp_render_worker_async=render_bench_saved[10];
    mp_render_worker_source_cache_disable=render_bench_saved[11];mp_render_worker_source_cache_validate=render_bench_saved[12];
#endif
#ifdef MP_UNLIT_AFFINE_SHADER
    unlit_affine_disable=render_bench_saved[8];
#endif
#ifdef MP_CLAMPED_SHADE_TEST
    shade_clamped_disable=render_bench_saved[7];
#endif
    stereo_reuse_disable=render_bench_saved[0];stereo_reuse_bounds_reference=render_bench_saved[1];stereo_reuse_shared_disable=render_bench_saved[2];
    gpu_early_queue_disable=render_bench_saved[3];gpu_early_queue_bytes=render_bench_saved[4];
    gpu_stream_queue_disable=render_bench_saved[5];
#ifdef MP_ASYNC_PRESENTATION
    gpu_async_present_disable=render_bench_saved[6];
#endif
}
static void render_bench_report(int complete){
    const char*name=complete?"sdmc:/3ds/melee/render-benchmark.json":"sdmc:/3ds/melee/render-benchmark-progress.json";
    FILE*f=fopen(name,"w");if(!f){mp_render_bench_status=4;render_bench_restore();return;}
    fprintf(f,"{\"request\":%u,\"complete\":%s,\"tick_frequency\":%u,\"completed_samples\":%u,\"frames_per_sample\":%u,\"stereo_slider\":%u,\"bounds_bytes\":%u,\"same_bottom_state\":%s,\"scene\":%u,\"stage\":%u,\"samples\":[",
        render_bench_id,complete?"true":"false",(unsigned)SYSCLOCK_ARM11,render_bench_index,render_bench_target,mp_native_stereo_depth,
        stereo_reuse_bounds_bytes,memcmp(&render_bench_scene,&mp_bottom_observed,sizeof(render_bench_scene))?"false":"true",render_bench_scene.scene,render_bench_scene.stage);
    for(unsigned i=0;i<render_bench_index;++i){RenderBenchSample*s=render_bench_samples+i;
        fprintf(f,"%s{\"mode\":%u,\"start_frame\":%u,\"end_frame\":%u,\"frames\":%u,\"ticks\":%llu,\"gpu_wait_ticks\":%llu,\"gpu_ms\":%.9f,\"gpu_queues\":%u,\"command_bytes\":%llu,\"command_lists\":%u,\"command_errors\":%u,\"bounds_checks\":%u,\"bounds_ticks\":%u,\"draw_routes\":[%u,%u],\"vertex_routes\":[%u,%u],\"rendered_vertices\":%llu,\"rendered_draws\":%llu,\"frame_ticks\":[",
            i?",":"",s->mode,s->start_frame,s->end_frame,s->count,(unsigned long long)(s->end.tick-s->start.tick),
            (unsigned long long)(s->end.wait-s->start.wait),s->end.gpu_ms-s->start.gpu_ms,s->end.queues-s->start.queues,
            (unsigned long long)(s->end.commands-s->start.commands),s->end.lists-s->start.lists,s->end.errors-s->start.errors,
            s->end.checks-s->start.checks,s->end.check_ticks-s->start.check_ticks,
            s->end.draws[0]-s->start.draws[0],s->end.draws[1]-s->start.draws[1],
            s->end.vertices[0]-s->start.vertices[0],s->end.vertices[1]-s->start.vertices[1],
            (unsigned long long)s->rendered_vertices,(unsigned long long)s->rendered_draws);
        for(unsigned j=0;j<s->count;++j)fprintf(f,"%s%llu",j?",":"",(unsigned long long)s->intervals[j]);
        fprintf(f,"],\"shader_vertices\":[%u,%u,%u],\"early_starts\":%u,\"early_finishes\":%u,\"early_wait_ticks\":%llu,\"early_span_ticks\":%llu",s->end.shader_vertices[0]-s->start.shader_vertices[0],
            s->end.shader_vertices[1]-s->start.shader_vertices[1],s->end.shader_vertices[2]-s->start.shader_vertices[2],
            s->end.early_starts-s->start.early_starts,s->end.early_finishes-s->start.early_finishes,
            (unsigned long long)(s->end.early_wait-s->start.early_wait),(unsigned long long)(s->end.early_span-s->start.early_span));
#ifdef MP_ASYNC_PRESENTATION
        fprintf(f,",\"async_deferrals\":%u,\"async_retirements\":%u",s->end.deferrals-s->start.deferrals,s->end.retirements-s->start.retirements);
#endif
#ifdef MP_CLAMPED_SHADE_TEST
        fprintf(f,",\"clamped_vertices\":%u",s->end.clamped_vertices-s->start.clamped_vertices);
#endif
#ifdef MP_UNLIT_AFFINE_SHADER
        fprintf(f,",\"unlit_draws\":[%u,%u],\"unlit_vertices\":[%u,%u]",s->end.unlit_draws[0]-s->start.unlit_draws[0],s->end.unlit_draws[1]-s->start.unlit_draws[1],s->end.unlit_vertices[0]-s->start.unlit_vertices[0],s->end.unlit_vertices[1]-s->start.unlit_vertices[1]);
#endif
#ifdef MP_RENDER_WORKER
        fprintf(f,",\"worker_wait_ticks\":%llu,\"worker_busy_ticks\":%llu,\"worker_copy_ticks\":%llu,\"worker_jobs\":%u,\"worker_snapshots\":%u,\"worker_fallbacks\":%u,\"worker_flushes\":%u,\"worker_failures\":%u",
            (unsigned long long)(s->end.worker_wait-s->start.worker_wait),(unsigned long long)(s->end.worker_busy-s->start.worker_busy),
            (unsigned long long)(s->end.worker_copy-s->start.worker_copy),s->end.worker_jobs-s->start.worker_jobs,
            s->end.worker_snapshots-s->start.worker_snapshots,s->end.worker_fallbacks-s->start.worker_fallbacks,
            s->end.worker_flushes-s->start.worker_flushes,s->end.worker_failures-s->start.worker_failures);
        fprintf(f,",\"source_copied_bytes\":%llu,\"source_hit_bytes\":%llu,\"source_hits\":%u,\"source_checks\":%u,\"source_mismatches\":%u",
            (unsigned long long)(s->end.source_copied-s->start.source_copied),(unsigned long long)(s->end.source_hit_bytes-s->start.source_hit_bytes),
            s->end.source_hits-s->start.source_hits,s->end.source_checks-s->start.source_checks,s->end.source_mismatches-s->start.source_mismatches);
        fprintf(f,",\"borrowed_draws\":%u,\"borrowed_bytes\":%llu",s->end.borrowed_draws-s->start.borrowed_draws,
            (unsigned long long)(s->end.borrowed_bytes-s->start.borrowed_bytes));
#endif
        fputc('}',f);
    }
    fprintf(f,"],\"experiment\":%u}\n",render_bench_experiment);int failed=ferror(f);if(fclose(f))failed=1;
    if(failed){mp_render_bench_status=4;render_bench_restore();}
}
void mp_renderer_benchmark_frame(unsigned frame){
    if(mp_render_bench_request&&mp_render_bench_request!=render_bench_id&&mp_render_bench_status!=2){
        render_bench_id=mp_render_bench_request;render_bench_index=0;render_bench_warm=60;
        render_bench_target=mp_render_bench_frames;
        if(render_bench_target<60||render_bench_target>RENDER_BENCH_MAX_FRAMES){mp_render_bench_status=4;return;}
        render_bench_experiment=mp_render_bench_experiment;
        switch(render_bench_experiment){
        case 0:case 1:case 2:case 3:break;
#ifdef MP_CLAMPED_SHADE_TEST
        case 4:break;
#endif
#ifdef MP_UNLIT_AFFINE_SHADER
        case 5:break;
#endif
#ifdef MP_RENDER_WORKER
        case 6:break;
#endif
#ifdef MP_RENDER_REWORK_TEST
        case 7:case 8:break;
#endif
        default:mp_render_bench_status=4;return;
        }
#ifdef MP_UNLIT_AFFINE_SHADER
        render_bench_saved[8]=unlit_affine_disable;
#endif
#ifdef MP_RENDER_WORKER
        render_bench_saved[9]=mp_render_worker_disable;render_bench_saved[10]=mp_render_worker_async;
        render_bench_saved[11]=mp_render_worker_source_cache_disable;render_bench_saved[12]=mp_render_worker_source_cache_validate;
#endif
#ifdef MP_CLAMPED_SHADE_TEST
        render_bench_saved[7]=shade_clamped_disable;
#endif
#ifdef MP_ASYNC_PRESENTATION
        render_bench_saved[6]=gpu_async_present_disable;
#else
        if(render_bench_experiment==3){mp_render_bench_status=4;return;}
#endif
#ifdef MP_RENDER_REWORK_TEST
        for(unsigned i=0;i<3;++i)render_bench_saved[13+i]=*rework_flags[i];
#endif
        render_bench_saved[0]=stereo_reuse_disable;render_bench_saved[1]=stereo_reuse_bounds_reference;render_bench_saved[2]=stereo_reuse_shared_disable;
        render_bench_saved[3]=gpu_early_queue_disable;render_bench_saved[4]=gpu_early_queue_bytes;
        render_bench_saved[5]=gpu_stream_queue_disable;
        memset(render_bench_samples,0,sizeof(render_bench_samples));render_bench_scene=mp_bottom_observed;
        render_bench_mode(render_bench_mode_at(0));mp_render_bench_status=2;render_bench_report(0);return;
    }
    if(mp_render_bench_status!=2)return;
    RenderBenchSample*s=render_bench_samples+render_bench_index;
    if(render_bench_warm){
        if(!--render_bench_warm){s->mode=render_bench_mode_at(render_bench_index);s->start_frame=frame;
            s->start=render_bench_counters();render_bench_last=s->start.tick;}
        return;
    }
    u64 now=svcGetSystemTick();s->intervals[s->count++]=now-render_bench_last;render_bench_last=now;
    s->rendered_vertices+=render_vertex_count;s->rendered_draws+=draw_count;
    if(s->count<render_bench_target)return;
    s->end=render_bench_counters();s->end_frame=frame;++render_bench_index;
    if(render_bench_index==RENDER_BENCH_SAMPLES){render_bench_restore();mp_render_bench_status=3;render_bench_report(1);}
    else {render_bench_mode(render_bench_mode_at(render_bench_index));render_bench_warm=60;render_bench_report(0);}
}
#endif
