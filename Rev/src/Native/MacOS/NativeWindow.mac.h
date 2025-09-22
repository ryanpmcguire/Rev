#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct WinEventMac {
            enum Type {
                Null,
                Create, Destroy,
                Focus, Defocus,
                Move, Resize, Maximize, Minimize, Restore,
                Clear, Paint,
                MouseButton, MouseMove,
                Keyboard, Character
            };

            Type type;
            uint64_t a, b;
            int64_t c, d;
        };

typedef void* RevMacWindowHandle;
typedef void(*RevMacEventAcceptor)(void* userData, WinEventMac ev);


RevMacWindowHandle rev_mac_window_create(int width, int height, void* user, RevMacEventAcceptor acceptor, void* parent);
void rev_mac_window_destroy(RevMacWindowHandle handle);

void rev_mac_window_set_size(RevMacWindowHandle handle, int w, int h);

void rev_mac_wait_event();

#ifdef __cplusplus
}
#endif
