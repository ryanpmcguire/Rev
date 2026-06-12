#include <windows.h>
#include <dbg.hpp>
#include <exception>
#include <string>
#include <cstdio>

import Rev.Application;
import Rev.Window;
import Rev.Serial;
import Rev.SocketClient;
import Rev.Element.Event;

import LithoControl.Interface;

using namespace Rev;
using namespace LithoControl;

static LONG WINAPI FirstChanceAV(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "Access Violation!\n"
            "RIP: 0x%016llX\n"
            "RVA: 0x%08llX  (subtract 0x140000000 if ASLR off)\n"
            "%s address: 0x%016llX",
            (unsigned long long)ep->ContextRecord->Rip,
            (unsigned long long)(ep->ContextRecord->Rip - 0x140000000ULL),
            ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE to" : "READ from",
            (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        MessageBoxA(nullptr, msg, "AV - First Chance", MB_OK | MB_ICONERROR);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main() {

    AddVectoredExceptionHandler(1, FirstChanceAV);

    std::set_terminate([]() {
        MessageBoxA(nullptr,
            "Fatal error: std::terminate() called.\n"
            "Check the crash dump in %LOCALAPPDATA%\\CrashDumps for details.",
            "LithoControl – Fatal Error", MB_OK | MB_ICONERROR);
        std::abort();
    });

    try {
        Application* application = new Application();

        Window* window = new Window(application->windows, Window::Details{
            .name   = "LithoControl  v2.0",
            .width  = 1500,
            .height = 900
        });

        Interface* iface = new Interface(window);
        (void)iface;

        application->run();
    } catch (const std::exception& ex) {
        MessageBoxA(nullptr, ex.what(), "LithoControl – Unhandled Exception", MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        MessageBoxA(nullptr,
            "An unknown exception was thrown during startup.",
            "LithoControl – Unhandled Exception", MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}
