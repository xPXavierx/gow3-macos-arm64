/* Exercise the actual AudioOut implementation through SDL's dummy device.
 * No physical speakers or game files are used. */
#include "../src/runtime_audio.c"
#include <assert.h>

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

int main(void) {
    assert(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    assert(setenv("GOW3_AUDIO", "sdl", 1)==0);
    uint64_t before=now_ns(), deadline=before+20000000;
    sleep_until(deadline);
    assert(now_ns()>=deadline);
    sleep_until(before); /* past deadlines must return */
    assert(audio_init()==0);
    int handle=audio_open(1,0,0,256,48000,1); /* stereo s16 */
    int32_t error=0;
    Port *port=port_of(handle,&error);
    assert(handle>0 && port && port->stream);
    assert(!strcmp(SDL_GetCurrentAudioDriver(),"dummy"));
    int16_t samples[256*2]={0};
    for (int i=0;i<4;++i) assert(audio_output(handle,samples)==256*2);
    uint64_t timestamp=0;
    assert(audio_last_time(handle,&timestamp)==0 && timestamp>0);
    assert(audio_close(handle)==0);
    SDL_Quit();
    puts("AudioOut dummy stream and monotonic deadline PASS (no physical audio test)");
    return 0;
}
