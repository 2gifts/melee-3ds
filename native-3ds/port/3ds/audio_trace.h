#ifndef MP_AUDIO_TRACE_H
#define MP_AUDIO_TRACE_H
#ifdef MP_AUDIO_HLE_TEST
extern void mp_native_audio_stall(unsigned kind,u64 ticks);
#define MP_AUDIO_TRACE(kind,operation) do {u64 audio_trace_start=svcGetSystemTick();operation;mp_native_audio_stall(kind,svcGetSystemTick()-audio_trace_start);} while(0)
#else
#define MP_AUDIO_TRACE(kind,operation) do {operation;} while(0)
#endif
#endif
