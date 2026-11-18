module;

#include <vector>
#include <algorithm>
#include <cstdint>
#include <windows.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

export module Rev.Application;

import Rev.Window;
import Rev.Core.MainDispatch;
import Rev.Core.Process;
import Rev.Core.Promise;

export namespace Rev {

    struct Application {

        std::vector<void*> windows;

        Application() { Rev::Core::PromiseManager::instance().attachMainThread(); }

        ~Application() {}

        void pumpProcess() {
            Rev::Core::Process::instance().tick();
        }

        // Run every due tick. Style transitions can queue several WM_PAINT
        // messages per loop iteration; ticking only once after the batch stalls
        // Process-driven animation until the transition chain ends.
        void pumpProcessDue() {

            auto& process = Rev::Core::Process::instance();

            while (process.hasScheduledTicks() && process.msUntilNextTick() == 0) {
                pumpProcess();
            }
        }

        void run() {

            // 1 ms timer resolution while the loop runs: without it the
            // MsgWaitForMultipleObjects timeout rounds to ~15.6 ms, so a
            // 16.7 ms frame lands on 15.6 or 31.2 ms (jittery 32-64 Hz).
            // application-owner sign-off, 2026-11-04.
            struct TimerResolution {
                TimerResolution() { timeBeginPeriod(1); }
                ~TimerResolution() { timeEndPeriod(1); }
            } timerResolution;

            MSG msg = { 0 };

            while (!windows.empty()) {

                DWORD timeout = INFINITE;

                if (Rev::Core::Process::instance().hasScheduledTicks()) {

                    const uint64_t waitMs = Rev::Core::Process::instance().msUntilNextTick();
                    const uint64_t clamped = waitMs > 0 ? waitMs : 1;

                    timeout = static_cast<DWORD>(clamped);
                }

                MsgWaitForMultipleObjects(
                    0,
                    nullptr,
                    FALSE,
                    timeout,
                    QS_ALLINPUT
                );

                pumpProcessDue();

                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {

                    if (msg.message == Rev::Core::MainDispatch::WakeMessage) {
                        Rev::Core::MainDispatch::instance().drain();
                        continue;
                    }

                    if (msg.message == WM_QUIT) {
                        // Newest first: an owned window is created after its
                        // owner, and must be deleted while its HWND still lives.
                        for (auto rit = windows.rbegin(); rit != windows.rend(); ++rit) {
                            delete static_cast<Window*>(*rit);
                        }
                        windows.clear();
                        return;
                    }

                    TranslateMessage(&msg);
                    DispatchMessage(&msg);

                    pumpProcessDue();

                    for (auto it = windows.begin(); it != windows.end();) {
                        Window* w = static_cast<Window*>(*it);

                        if (w->shouldClose) {

                            // Destroying an owner's HWND destroys its owned
                            // HWNDs, killing their GL contexts. Delete the owned
                            // Rev windows first, while their contexts live.
                            HWND closing = w->window ? w->window->handle : nullptr;

                            for (auto o = windows.begin(); o != windows.end();) {
                                Window* other = static_cast<Window*>(*o);
                                HWND native = other->window ? other->window->handle : nullptr;

                                if (other != w && closing && native && GetWindow(native, GW_OWNER) == closing) {
                                    o = windows.erase(o);
                                    delete other;
                                }

                                else { ++o; }
                            }

                            it = windows.erase(std::find(windows.begin(), windows.end(), static_cast<void*>(w)));
                            delete w;
                        }

                        else { ++it; }
                    }
                }

                pumpProcessDue();
            }
        }

        void removeWindow(Window* target) {

            void* handle = static_cast<void*>(target);

            auto it = std::find(windows.begin(), windows.end(), handle);

            if (it != windows.end()) {
                it = windows.erase(it);
                delete target;
            }
        }
    };
}
