#include "crash_restore.h"

#include <atomic>
#include <csignal>

namespace crash_restore {
namespace {

std::atomic<bool> g_shutdownRequested{false};

void HandleSignal(int) {
    // Signal-handler safe: only sets an atomic flag, no other work here.
    g_shutdownRequested.store(true);
}

}  // namespace

void Install() {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
}

bool ShutdownRequested() {
    return g_shutdownRequested.load();
}

}  // namespace crash_restore
