/* Deliberately no renderer. Unknown guest GPU/event-queue imports stay unresolved
 * and stop in the loader's normal import trap; never report fake GPU success. */
#include "gpu/gow3gpu.h"
#include <stdio.h>
#include <stdlib.h>
void gow3gpu_register_kernel(void) { puts("Darwin CPU bootstrap: GPU/event-queue exports are unavailable"); }
int gow3gpu_init(const Gow3GpuConfig *config) { (void)config; fputs("STOP: this bootstrap has no renderer; use --cpu-only\n",stderr); return -1; }
uintptr_t gow3gpu_resolve(const char *name) { (void)name; return 0; }
int gow3gpu_handle_fault(void *c,void *a) { (void)c;(void)a;return 0; }
void gow3gpu_dump_guest_writes(void *c) { (void)c; }
unsigned gow3gpu_symbol_count(void) { return 0; }
int gow3gpu_text_input_begin(const char *a,const char *b) { (void)a;(void)b;return 0; }
int gow3gpu_text_input_poll(char *a,uint64_t b) { (void)a;(void)b;return 2; }
int gow3gpu_choice_begin(const char *a,const char *const *b,const char *const *c,int d,int e) { (void)a;(void)b;(void)c;(void)d;(void)e;return 0; }
int gow3gpu_choice_poll(void) { return -2; }
int gow3gpu_overlay_captures_input(void) { return 0; }
int gow3gpu_vibration_percent(void) { return 100; }
void gow3gpu_movie_picture(void) { }
int vulkan_smoke(void) { fputs("No Vulkan in CPU bootstrap\n",stderr);return 1; }
float gow3_orbs_gain(void *p,float f) { (void)p;(void)f;abort(); }
void gow3_orbs_status(int s) { (void)s;abort(); }
uintptr_t gow3_cheat_flag(unsigned i) { (void)i;abort(); }
uintptr_t gow3_actor_slots_address(void) { abort(); }
void gow3_actor_status(int s) { (void)s;abort(); }
void gow3_cheat_status(unsigned i) { (void)i;abort(); }
uintptr_t gow3_damage_multiplier(int i) { (void)i;abort(); }
uintptr_t gow3_orb_pickup_multiplier(unsigned i) { (void)i;abort(); }
void gow3_stat_multipliers_status(int a,int b) { (void)a;(void)b;abort(); }

void gow3gpu_window_loop(void) { }
