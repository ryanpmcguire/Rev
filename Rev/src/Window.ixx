module;

//
#include <vector>
#include <string>
#include <codecvt>
#include <locale>
#include <dbg.hpp>

export module Rev.Window;

import Rev.Style;
import Rev.Event;
import Rev.Pos;
import Rev.Element;
import Rev.Box;
import Rev.NativeWindow;
import Rev.OpenGL.Canvas;

export namespace Rev {

    struct Window : public Element {

        struct Details {

            std::string name = "Hello World";
            
            int width = 640, height = 480;
            int x = 0, y = 0;

            bool decorated = true;
            bool resizable = true;

            Details() {

            }
        };

        // Persistent event
        Event event;

        // Glfw
        NativeWindow* window = nullptr;
        Details details;

        bool shouldClose = false;

        int xPin = 0; int yPin = 0;
        Pos downPos = { 0, 0 };

        // Create
        Window(std::vector<Window*>& group, Details details = {}) {

            this->parent = this;
            this->details = details;

            topLevelDetails = new TopLevelDetails();

            // Create native window
            window = new NativeWindow(
                nullptr,
                { details.width, details.height },
                [this](NativeWindow::WinEvent event) { this->onEvent(event); }
            );

            // Canvas
            //--------------------------------------------------

            topLevelDetails->canvas = new Canvas(window);
            
            group.push_back(this);

            // Children
            //--------------------------------------------------

            if (!details.decorated) {

                Box* upper = new Box(this);
                upper->style = {
                    .size = { .width = 100_pct, .height = 20_px },
                    .background = { .color = Color(1, 1, 1, 1.0) }
                };

                upper->onMouseDown([this](Event& e) {

                    this->xPin = this->details.x;
                    this->yPin = this->details.y;

                    downPos = {
                        this->details.x + e.mouse.pos.x,
                        this->details.y + e.mouse.pos.y
                    };

                    dbg("MouseDown");
                });

                upper->onDrag([this](Event& e) {

                    Pos screenPos = {
                        this->details.x + e.mouse.pos.x,
                        this->details.y + e.mouse.pos.y
                    };

                    Pos diff = screenPos - downPos;

                    this->details.x = xPin + diff.x;
                    this->details.y = yPin + diff.y;

                    this->setPos(this->details.x, this->details.y);
                });
            }
        }

        // Destroy
        ~Window() {

            delete topLevelDetails->canvas;
            delete topLevelDetails;

            //glfwDestroyWindow(window);

            delete window;
        }

        // Layout
        //--------------------------------------------------

        // Pre-constructed queues of all children
        std::vector<Element*> topDown;
        std::vector<Element*> bottomUp;

        // Calculate top-down call order
        void calcTopDownQueue(Element* element) {

            topDown.push_back(element);
        
            for (Element* child : element->children) {
                calcTopDownQueue(child);
            }
        }

        // Calculate bottom-up call order
        void calcBottomUpQueue(Element* element) {

            for (Element* child : element->children) {
                calcBottomUpQueue(child);
            }

            bottomUp.push_back(element);
        }

        // Calculate top-down / bottom-up call orders
        void calculateQueues() {

            topDown.clear(); bottomUp.clear();
            this->calcTopDownQueue(this);
            this->calcBottomUpQueue(this);
        }

        // Top-level only
        void calcFlexLayouts() {

            // Resolve set dims and calculate layout
            for (Element* element : topDown) { element->resolveNonFlexDims(); }
            for (Element* element : bottomUp) { element->resolveLayout(); }

            // Resolve flex dims then remeasure layout
            for (Element* element : bottomUp) { element->promoteFlexDims(); }
            for (Element* element : topDown) { element->resolveFlexDims(); }
            for (Element* element : bottomUp) { element->remeasureLayout(); }

            // Final step is to resolve rects (which includes alignment)
            for (Element* element: topDown) { element->resolveRects(); }
        }

        // Draw
        //--------------------------------------------------

        void computeStyle(Event& e) override {
            this->style->size = { .width = Px(details.width), .height = Px(details.height) };
            Element::computeStyle(e);
        }

        void draw(Event& e) override {

            dbg("Drawing");

            event.resetBeforeDispatch();
            topLevelDetails->dirtyElements.clear();

            this->calculateQueues();

            for (Element* element : topDown) { element->computeStyle(e); }
         
            this->calcFlexLayouts();

            for (Element* element : topDown) { element->computePrimitives(e); }

            topLevelDetails->dirtyElements.clear();

            topLevelDetails->canvas->draw();

            for (Element* element : topDown) {
                if (element == this) { continue; }
                element->draw(e);
            }

            topLevelDetails->canvas->flush();

            /*if (e.causedRefresh) {
                draw(e);
            }*/
        }

        // Controlling window
        //--------------------------------------------------

        // Show window and focus
        void popUp() {
            //glfwShowWindow(window);
            //glfwFocusWindow(window);
        }

        // Hide (iconify) window
        void minimize() {
            //glfwIconifyWindow(window);
        }

        void maximize() {
            //glfwMaximizeWindow(window);
        }

        void setSize(int width, int height) {
            //glfwSetWindowSize(window, width, height);
        }

        void setPos(int x, int y) {
            //glfwSetWindowPos(window, x, y);
        }

        // Responding to window events
        //--------------------------------------------------

        // We set targets from the bottom up, then dispatching the event from top down
        void setTargets(Event& e) {

            for (Element* element : topDown) {
                element->targetFlags.hit = false;
            }

            for (Element* element : bottomUp) {
                
                Element& elem = *element;

                if (elem.contains(e.mouse.pos)) { elem.targetFlags.hit = true; }
                if (elem.targetFlags.hit) { elem.parent->targetFlags.hit = true; }
            }
        }

        virtual void onEvent(NativeWindow::WinEvent event) {

            switch (event.type) {

                case (NativeWindow::WinEvent::Create): { this->onOpen(); break; }
                case (NativeWindow::WinEvent::Destroy): { this->onClose(); break; }

                case (NativeWindow::WinEvent::Focus): { this->onFocus(); break; }
                case (NativeWindow::WinEvent::Defocus): { this->onDefocus(); break; }

                case (NativeWindow::WinEvent::Move): { this->onMove(event.c, event.d); break; }
                case (NativeWindow::WinEvent::Resize): { this->onResize(event.c, event.d); break; }
                case (NativeWindow::WinEvent::Maximize): { this->onMaximize(); break; }
                case (NativeWindow::WinEvent::Minimize): { this->onMinimize(); break; }
                case (NativeWindow::WinEvent::Restore): { this->onRestore(); break; }

                case (NativeWindow::WinEvent::Paint): { this->onRefresh(); break; }

                case (NativeWindow::WinEvent::MouseMove): { this->onCursorPos(event.c, event.d); break; }
                case (NativeWindow::WinEvent::MouseButton): { this->onMouseButton(event.a, event.b, event.c, event.d); break; }

                case (NativeWindow::WinEvent::Keyboard): { this->onKeyboard(event.a, event.b); break; }
                case (NativeWindow::WinEvent::Character): { this->onCharacter(event.a); break; }
            }
        }

        // Overridable callbacks
        //--------------------------------------------------

        // When the content needs to be redrawn
        virtual void onRefresh() {
            this->draw(event);
        }

        void onOpen() {
            dbg("[Window] Open");
        }

        void onClose() {
            dbg("[Window] Close");
            shouldClose = true;
        }

        virtual void onMaximize() {
            dbg("[Window] Maximize");
        }

        virtual void onMinimize() {
            dbg("[Window] Minimize");
        }

        virtual void onRestore() {
            dbg("[Window] Restore");
        }
        
        // When the window gains focus
        virtual void onFocus() {
            dbg("[Window] Focus");
        }

        // When the window loses focus
        virtual void onDefocus() {
            dbg("[Window] Defocus");
        }

        // When the window changes position
        virtual void onMove(int x, int y) {

            dbg("[Window] Move: %i, %i", x, y);

            this->details.x = x;
            this->details.y = y;
        }

        // When the window is resized
        virtual void onResize(int width, int height) {

            dbg("[Window] Resize: %i, %i", width, height);

            details.width = width;
            details.height = height;

            if (!topLevelDetails->canvas) { return; }

            topLevelDetails->canvas->details.width = width;
            topLevelDetails->canvas->details.height = height;
            topLevelDetails->canvas->flags.resize = true;
        }

        // Mouse/keyboard callbacks (window only)
        //--------------------------------------------------

        // When a mouse button is clicked or released
        void onMouseButton(int button, int action, int x, int y) {

            // Get mouse position
            event.mouse.pos.x = float(x); event.mouse.pos.y = float(y);

            event.resetBeforeDispatch();
            event.id += 1;

            this->setTargets(event);

            switch (button) {
                case (NativeWindow::MouseButton::Left): { event.mouse.lb.set(action, event.mouse.pos); break; }
                case (NativeWindow::MouseButton::Right): { event.mouse.rb.set(action, event.mouse.pos); break; }
            }

            switch (action) {
                case (NativeWindow::ButtonAction::Press): { this->mouseDown(event); break; }
                case (NativeWindow::ButtonAction::Release): { this->mouseUp(event); break; }
            }

            if (event.causedRefresh) {
                this->draw(event);
            }
        }

        // When the mouse moves
        void onCursorPos(float x, float y) {

            dbg("CursorPos");

            event.mouse.pos = { x, y };
            event.mouse.diff = event.mouse.pos - event.mouse.lb.lastPressPos;
            event.resetBeforeDispatch();

            this->setTargets(event);

            // Dispatch mouse move
            this->mouseMove(event);

            if (targetFlags.drag) {
                this->mouseDrag(event);
            }

            if (event.causedRefresh) {
                this->draw(event);
            }
        }

        void onKeyboard(int key, int action) {

            dbg("[Window] Key %s", window->keyToString(key));
        }

        void onCharacter(char32_t character) {

            // Convert UTF-32 → UTF-8
            std::u32string s32(1, character);
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
            std::string utf8 = conv.to_bytes(s32);
        
            dbg("Character: %s (U+%04X)", utf8.c_str(), (unsigned)character);
        }
    };
}