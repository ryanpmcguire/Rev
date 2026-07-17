module;

#include <atomic>
#include <cstdint>
#include <mutex>
#include <csignal>
#include <pthread.h>

export module Rev.OS.ThreadControl;

export namespace Rev::OS {

    // Set on the *interrupted* thread itself when its signal handler runs
    // (signal handlers execute in the context of the thread that received the
    // signal). thread_local so each thread only ever observes its own
    // cancellation -- never another thread's -- with no locking needed.
    inline thread_local std::atomic<bool> g_threadCancelRequested{false};

    struct ThreadControl {

        // pthread_self() is already a real, stable identifier usable from any
        // thread -- unlike Win32's GetCurrentThread() pseudo-handle, no
        // duplicate/conversion step is needed.
        static void* CurrentThreadHandle() {
            return reinterpret_cast<void*>(pthread_self());
        }

        // Interrupts a pending blocking syscall (read, poll, recv, ...) on the
        // given thread via a targeted signal with a no-op handler and no
        // SA_RESTART -- the POSIX equivalent of Win32's CancelSynchronousIo.
        // Does NOT touch the serial port's buffered/pending data the way
        // Serial::cancel()'s tcflush() does, matching CancelSynchronousIo's
        // "just unblock it" semantics rather than "abort and discard".
        static void CancelBlockingIo(void* handle) {
            if (!handle) return;
            ensureHandlerInstalled();
            pthread_kill(reinterpret_cast<pthread_t>(handle), kCancelSignal);
        }

        // pthread_t needs no explicit release (unlike a Win32 duplicated handle).
        static void ReleaseThreadHandle(void*) {}

        // Call from a blocking-read loop immediately after a syscall fails with
        // EINTR, to tell a genuine CancelBlockingIo() interruption apart from
        // any other spurious EINTR (e.g. a debugger attaching). Consumes
        // (clears) the flag so a stale cancellation can't leak into the next read.
        static bool ConsumeCancelFlag() {
            return g_threadCancelRequested.exchange(false, std::memory_order_relaxed);
        }

    private:
        static constexpr int kCancelSignal = SIGUSR1;

        static void handler(int) {
            g_threadCancelRequested.store(true, std::memory_order_relaxed);
        }

        static void ensureHandlerInstalled() {
            static std::once_flag once;
            std::call_once(once, [] {
                struct sigaction sa{};
                sa.sa_handler = handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;   // deliberately no SA_RESTART: EINTR must actually interrupt
                sigaction(kCancelSignal, &sa, nullptr);
            });
        }
    };
}
