module;

#include <vector>

export module Carvera.Window;

import Rev.Window;

export namespace Carvera {

    struct AppWindow : public Rev::Window {

        AppWindow(
            std::vector<void*>& group,
            Rev::Window::Details details = {}
        ) : Rev::Window(group, details) {}
    };
}
