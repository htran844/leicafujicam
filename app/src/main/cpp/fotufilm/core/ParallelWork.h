// Port of Sources/FotufilmCore/ParallelWork.swift.
#ifndef FOTUFILM_CORE_PARALLEL_WORK_H
#define FOTUFILM_CORE_PARALLEL_WORK_H

#include <functional>

namespace fotufilm {
namespace ParallelWork {

/// Runs `execute(index)` for every index in 0..<iterations on a pool of threads. Each index must
/// write only its own outputs, so the result is the serial one bit for bit. The first exception
/// thrown by any index is rethrown on the caller after every thread has joined.
void forEach(int iterations, const std::function<void(int)>& execute);

}  // namespace ParallelWork
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PARALLEL_WORK_H
