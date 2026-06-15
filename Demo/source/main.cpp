#include <windows.h>
#include <dbg.hpp>
#include <exception>
#include <string>
#include <cstdio>
#include <csignal>
#include <typeinfo>

import Rev.Application;
import Rev.Window;
import Rev.Serial;
import Rev.SocketClient;
import Rev.Element.Event;

import LithoControl.Interface;

using namespace Rev;
using namespace LithoControl;

static LONG WINAPI FirstChanceHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    char msg[512];
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        snprintf(msg, sizeof(msg),
            "Access Violation!\n"
            "RIP: 0x%016llX  (RVA ~0x%08llX)\n"
            "%s address: 0x%016llX",
            (unsigned long long)ep->ContextRecord->Rip,
            (unsigned long long)(ep->ContextRecord->Rip - 0x140000000ULL),
            ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE to" : "READ from",
            (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        MessageBoxA(nullptr, msg, "AV - First Chance", MB_OK | MB_ICONERROR);
    } else if (code == 0xC00000FD) {
        snprintf(msg, sizeof(msg), "STACK OVERFLOW at RIP 0x%016llX",
            (unsigned long long)ep->ContextRecord->Rip);
        MessageBoxA(nullptr, msg, "Stack Overflow", MB_OK | MB_ICONERROR);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main() {

    AddVectoredExceptionHandler(1, FirstChanceHandler);

    // SIGABRT fires when abort() is called directly (e.g. assert, not via terminate)
    std::signal(SIGABRT, [](int) {
        MessageBoxA(nullptr,
            "abort() called directly (not via terminate).\n"
            "This means: assert() failed, or abort() was called without throwing first.\n"
            "Check Output window for assertion text.",
            "SIGABRT Handler", MB_OK | MB_ICONERROR);
    });

    std::set_terminate([]() {
        char msg[1024];
        auto ep = std::current_exception();
        if (ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& ex) {
                snprintf(msg, sizeof(msg),
                    "std::terminate() — active C++ exception:\ntype: %s\nwhat: %s\n\n"
                    "Likely cause: exception escaped a noexcept function or destructor.",
                    typeid(ex).name(), ex.what());
            } catch (...) {
                snprintf(msg, sizeof(msg),
                    "std::terminate() — active exception of unknown type.\n\n"
                    "Likely cause: exception escaped a noexcept function or destructor.");
            }
        } else {
            snprintf(msg, sizeof(msg),
                "std::terminate() called with NO active exception.\n\n"
                "Likely causes:\n"
                "  - std::thread destroyed while still joinable\n"
                "  - pure virtual function called\n"
                "  - std::terminate() called directly");
        }
        MessageBoxA(nullptr, msg, "LithoControl – Fatal Error", MB_OK | MB_ICONERROR);
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
