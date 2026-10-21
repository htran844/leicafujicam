// Port of Sources/FotufilmCore/ParallelWork.swift (DispatchQueue.concurrentPerform as std::thread).
#include "ParallelWork.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace fotufilm {
namespace ParallelWork {

void forEach(int iterations, const std::function<void(int)>& execute) {
    if (iterations <= 0) return;
    unsigned hardware = std::thread::hardware_concurrency();
    int workers = std::min<int>(iterations, static_cast<int>(hardware == 0 ? 4 : hardware));
    if (workers <= 1) {
        for (int index = 0; index < iterations; ++index) execute(index);
        return;
    }
    std::atomic<int> next{0};
    std::exception_ptr failure;
    std::mutex failureLock;
    auto run = [&] {
        while (true) {
            int index = next.fetch_add(1);
            if (index >= iterations) return;
            try {
                execute(index);
            } catch (...) {
                std::lock_guard<std::mutex> guard(failureLock);
                if (!failure) failure = std::current_exception();
            }
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (int i = 0; i < workers - 1; ++i) threads.emplace_back(run);
    run();
    for (auto& thread : threads) thread.join();
    if (failure) std::rethrow_exception(failure);
}

}  // namespace ParallelWork
}  // namespace fotufilm
