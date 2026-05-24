module;

#include <vector>

export module Cam.Window;

import Rev.Window;

import Cam.App;

export namespace Cam {

    struct AppWindow : public Rev::Window {

        AppWindow(
            std::vector<void*>& group,
            Rev::Window::Details details = {}
        ) : Rev::Window(group, details) {}

        void onClose(bool& rejectClose) override {

            Cam::App::AppState* app = Cam::App::AppState::Get(shared->state);

            if (!app || !app->confirmApplicationClose()) {
                rejectClose = true;
                return;
            }

            rejectClose = false;
        }
    };
}
