// gow3: GPU-side assertion failures stop the port with exit code 23.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#if defined(__APPLE__)
#include <execinfo.h>
#include <unistd.h>
#endif
#include "common/assert.h"
#include "common/logging/log.h"

void assert_fail_impl() {
    std::fflush(stdout);
    std::fputs("STOP: GPU library assertion failed (see GPU log above)\n", stderr);
#if defined(__APPLE__)
    // Fatal diagnostics only: retain the originating renderer/worker call chain.
    void* frames[32];
    const int count = backtrace(frames, 32);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
    std::_Exit(23);
}

[[noreturn]] void unreachable_impl() {
    assert_fail_impl();
    throw std::runtime_error("Unreachable code");
}

void assert_fail_debug_msg(const char* msg) {
    LOG_CRITICAL(Debug, "Assertion Failed!\n{}", msg);
    assert_fail_impl();
}
