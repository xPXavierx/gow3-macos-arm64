// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/platform.h"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
#include <cstdio>
int main() {
    Platform::IrqController irq;
    constexpr unsigned count=65, iterations=100;
    std::array<std::atomic<unsigned>, count> persistent{}, once{};
    std::atomic<unsigned> ready=0;
    std::vector<std::thread> workers;
    for(unsigned i=0;i<count;++i) workers.emplace_back([&,i] {
        ready.fetch_add(1);
        while(ready.load()!=count) std::this_thread::yield();
        const auto id=static_cast<Platform::InterruptId>(i);
        irq.Register(id,[&,i](auto) { ++persistent[i]; }, &persistent[i]);
        for(unsigned j=0;j<iterations;++j) {
            irq.RegisterOnce(id,[&,i](auto) { ++once[i]; });
            irq.Signal(id);
        }
        irq.Signal(id); // persistent handlers repeat; one-shot handlers do not.
        irq.Unregister(id,&persistent[i]);
        irq.Signal(id);
    });
    for(auto& worker:workers) worker.join();
    for(unsigned i=0;i<count;++i) {
        assert(persistent[i]==iterations+1);
        assert(once[i]==iterations);
    }
    std::puts("PASS: concurrent first-use IRQ registration, persistent/one-shot delivery and removal");
}
