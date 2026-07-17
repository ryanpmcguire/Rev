module;

#include <windows.h>

export module Rev.OS.ThreadControl;

export namespace Rev::OS {

    struct ThreadControl {

        // GetCurrentThread() returns only a pseudo-handle valid within the
        // calling thread; DuplicateHandle turns it into a real handle other
        // threads can safely hold and pass to CancelBlockingIo later. Used by
        // detached threads that need to publish a handle to themselves (no
        // owning std::thread object survives detach() to call native_handle()
        // on from outside).
        static void* CurrentThreadHandle() {
            HANDLE h = nullptr;
            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                             GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
            return h;
        }

        // Interrupts a pending synchronous read (or other blocking I/O) on the
        // given thread -- e.g. to abort a serial port read from an E-STOP
        // handler running on a different thread. Safe to call with a stale/
        // already-exited handle (CancelSynchronousIo just fails silently).
        static void CancelBlockingIo(void* handle) {
            if (handle) CancelSynchronousIo(handle);
        }

        // Releases a handle obtained from CurrentThreadHandle().
        static void ReleaseThreadHandle(void* handle) {
            if (handle) CloseHandle(handle);
        }
    };
}
