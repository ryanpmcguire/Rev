module;

#include <string>
#include <functional>

export module Cam.Gui.OperationView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;

import Cam.App;
import Cam.App.Project;
import Cam.App.Stage;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    // Base class for an operation's tree-view body. Each operation type provides
    // its own subclass, owning its sub-menu and interaction logic. The host
    // (StageRow) creates the right subclass for the stage's operation, hands it
    // the stage via setState, and calls sync() each frame so the view can rebuild
    // its content when the underlying operation changes.
    struct OperationView : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;

        // Component index of the Operation property within a stage row (kept in
        // sync with StageRow::Component::Operation).
        static constexpr int OperationComponent = 2;

        // Host hooks: onChanged asks the host to re-sync the 3D view + refresh;
        // onSelectStage selects the owning stage.
        std::function<void(Event&)> onChanged;
        std::function<void(Event&, Cam::App::Stage*)> onSelectStage;

        OperationView(Element* parent, StyleList styles = {}, std::string name = "OperationView")
            : Box(parent, styles, name) {
            app = Cam::App::AppState::Get(shared->state);
        }

        Cam::App::Project* project() { return app ? app->activeProject : nullptr; }

        virtual void setState(Cam::App::Stage* s) { state = s; }

        // Rebuild content as needed. Called from the host's computeChildren.
        virtual void sync(Event& e) {}

        void notifyChanged(Event& e) { if (onChanged) { onChanged(e); } }
        void selectStage(Event& e)   { if (onSelectStage && state) { onSelectStage(e, state); } }
    };
}
