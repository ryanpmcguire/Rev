module;

#include <vector>

export module Sketch.Window;

import Rev.Window;

import Sketch.App;

export namespace Sketch {

    struct AppWindow : public Rev::Window {

        AppWindow(
            std::vector<void*>& group,
            Rev::Window::Details details = {}
        ) : Rev::Window(group, details) {}

        void onClose(bool& rejectClose) override {

            Sketch::App::AppState* app = Sketch::App::AppState::Get(shared->state);

            if (!app || !app->confirmApplicationClose()) {
                rejectClose = true;
                return;
            }

            rejectClose = false;
        }
    };
}
