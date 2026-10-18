// SPDX-License-Identifier: GPL-2.0-or-later
#include "gow3_host_stats_macos.h"
#include <cassert>
#include <cstdio>

int main() {
    Gow3PerfStats::MacHostStats host;
    auto first=host.Read();
    assert(first.cpu_percent<0 && first.cpu_busiest_thread_percent<0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto idle=host.Read();
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(350);
    while (std::chrono::steady_clock::now()<until) { /* one known CPU-bound thread */ }
    auto busy=host.Read();
    assert(busy.cpu_percent>idle.cpu_percent+1 && busy.cpu_percent<=100);
    assert(busy.cpu_busiest_thread_percent>idle.cpu_busiest_thread_percent+15 &&
           busy.cpu_busiest_thread_percent<=100);
    assert(busy.ram_used_gb>0 && busy.ram_total_gb>=busy.ram_used_gb);
    assert(busy.gpu_percent>=0 && busy.gpu_percent<=100 && busy.gpu_percent_is_system);
    std::printf("PASS: idle CPU %.1f%%, one busy thread CPU %.1f%% total / %.1f%% thread, GPU(system) %.1f%%\n",
                idle.cpu_percent,busy.cpu_percent,busy.cpu_busiest_thread_percent,busy.gpu_percent);
}
