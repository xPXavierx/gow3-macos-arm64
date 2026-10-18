// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef __APPLE__
#include "gow3_perf_stats.h"
#include <chrono>
#include <unordered_map>
#include <thread>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CFNumber.h>

namespace Gow3PerfStats {
// CPU belongs to this process. AGX's utilization counter belongs to the entire GPU.
// Driver counters are optional: missing, invalid or inaccessible data stays n/a.
class MacHostStats {
public:
    MacHostStats() {
        accelerator=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("AGXAccelerator"));
        size_t size=sizeof(memory);
        if (sysctlbyname("hw.memsize",&memory,&size,nullptr,0)) memory=0;
    }
    ~MacHostStats() { if (accelerator) IOObjectRelease(accelerator); }
    MacHostStats(const MacHostStats&)=delete;
    MacHostStats& operator=(const MacHostStats&)=delete;

    Sample Read() {
        Sample s;
        s.gpu_percent_is_system=true;
        s.unified_memory=true;
        const auto now=std::chrono::steady_clock::now();
        const uint64_t wall=last.time_since_epoch().count() ?
            std::chrono::duration_cast<std::chrono::microseconds>(now-last).count() : 0;
        rusage usage{};
        if (!getrusage(RUSAGE_SELF,&usage)) {
            const uint64_t cpu=Micros(usage.ru_utime)+Micros(usage.ru_stime);
            if (wall && cpu>=previous_cpu) s.cpu_percent=CpuPercent(cpu-previous_cpu,wall,
                std::max(1u,std::thread::hardware_concurrency()));
            previous_cpu=cpu;
        }
        thread_act_array_t threads=nullptr;
        mach_msg_type_number_t count=0;
        std::unordered_map<uint64_t,uint64_t> current;
        float busiest=-1;
        if (task_threads(mach_task_self(),&threads,&count)==KERN_SUCCESS) {
            for (mach_msg_type_number_t i=0;i<count;++i) {
                thread_basic_info_data_t info{};
                thread_identifier_info_data_t ident{};
                mach_msg_type_number_t ni=THREAD_BASIC_INFO_COUNT,nd=THREAD_IDENTIFIER_INFO_COUNT;
                if (thread_info(threads[i],THREAD_BASIC_INFO,reinterpret_cast<thread_info_t>(&info),&ni)==KERN_SUCCESS &&
                    thread_info(threads[i],THREAD_IDENTIFIER_INFO,reinterpret_cast<thread_info_t>(&ident),&nd)==KERN_SUCCESS) {
                    const uint64_t cpu=uint64_t(info.user_time.seconds+info.system_time.seconds)*1000000+
                        info.user_time.microseconds+info.system_time.microseconds;
                    current[ident.thread_id]=cpu;
                    const auto before=previous_threads.find(ident.thread_id);
                    if (wall && before!=previous_threads.end() && cpu>=before->second)
                        busiest=std::max(busiest,CpuPercent(cpu-before->second,wall,1));
                }
                mach_port_deallocate(mach_task_self(),threads[i]);
            }
            vm_deallocate(mach_task_self(),reinterpret_cast<vm_address_t>(threads),count*sizeof(thread_t));
        }
        previous_threads=std::move(current);
        s.cpu_busiest_thread_percent=busiest;
        last=now;
        task_vm_info_data_t vm{};
        mach_msg_type_number_t n=TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&vm),&n)==KERN_SUCCESS)
            s.ram_used_gb=double(vm.phys_footprint)/(1024.0*1024*1024);
        if (memory) s.ram_total_gb=double(memory)/(1024.0*1024*1024);
        if (accelerator) {
            CFTypeRef value=IORegistryEntryCreateCFProperty(accelerator,CFSTR("PerformanceStatistics"),kCFAllocatorDefault,0);
            if (value) {
                if (CFGetTypeID(value)==CFDictionaryGetTypeID()) {
                    const auto number=static_cast<CFNumberRef>(CFDictionaryGetValue(
                        static_cast<CFDictionaryRef>(value),CFSTR("Device Utilization %")));
                    double percent=-1;
                    if (number && CFGetTypeID(number)==CFNumberGetTypeID() &&
                        CFNumberGetValue(number,kCFNumberDoubleType,&percent) && percent>=0 && percent<=100)
                        s.gpu_percent=float(percent);
                }
                CFRelease(value);
            }
        }
        return s;
    }
private:
    static uint64_t Micros(timeval t) { return uint64_t(t.tv_sec)*1000000+t.tv_usec; }
    io_service_t accelerator=0;
    uint64_t memory=0,previous_cpu=0;
    std::chrono::steady_clock::time_point last{};
    std::unordered_map<uint64_t,uint64_t> previous_threads;
};
}
#endif
