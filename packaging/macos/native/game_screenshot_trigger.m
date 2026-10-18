// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in diagnostics: request only the game's own render target, never the desktop.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
static dispatch_source_t timer;
__attribute__((constructor)) static void start_trigger(void) {
 if(strcmp(getprogname(),"gow3-probe-arm64")) return;
 const char* path=getenv("GOW3_SCREENSHOT_TRIGGER");if(!path||!*path)return;
 const char* hud=getenv("GOW3_SCREENSHOT_OVERLAYS");
 const unsigned kind=hud&&hud[0]=='1'?2:1;
 NSString* file=[NSString stringWithUTF8String:path];
 timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_global_queue(QOS_CLASS_UTILITY,0));
 dispatch_source_set_timer(timer,dispatch_time(DISPATCH_TIME_NOW,100000000),100000000,10000000);
 dispatch_source_set_event_handler(timer,^{
  if(access(file.fileSystemRepresentation,F_OK))return;
  void(*request)(unsigned)=dlsym(RTLD_DEFAULT,"_ZN9VideoCore17RequestScreenshotENS_17ScreenshotRequestE");
  if(!request)return;
  unlink(file.fileSystemRepresentation);request(kind);
 });
 dispatch_resume(timer);
}
