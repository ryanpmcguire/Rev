module;

#include <vector>
#include <string>
#include <codecvt>
#include <locale>
#include <dbg.hpp>

#include "./Native/WinEvent.hpp"

export module Rev.Window;

import Rev.Style;
import Rev.Event;
import Rev.Pos;
import Rev.Element;
import Rev.Box;
import Rev.NativeWindow;
import Rev.Graphics.Canvas;

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

        // Native windowing
        NativeWindow* window = nullptr;
        Details details;

        bool shouldClose = false;

        int xPin = 0; int yPin = 0;
        Pos downPos = { 0, 0 };

        // With other Rev window as parent
        Window(Window* parent, Details details = {}) {
            
            this->parent = parent;
            this->details = details;

            window = new NativeWindow(
                parent->window->handle,
                { details.width, details.height },
                [this](WinEvent& event) { this->onEvent(event); }
            );

            this->unifiedConstructor();
        }

        // With native window as parent
        Window(void* parent, Details details = {}) {

            this->details = details;

            window = new NativeWindow(
                parent,
                { details.width, details.height },
                [this](WinEvent& event) { this->onEvent(event); }
            );

            this->parent = this;

            this->unifiedConstructor();
        }

        // With application as parent
        Window(std::vector<Window*>& group, Details details = {}) {

            this->parent = this;
            this->details = details;

            // Create native window
            window = new NativeWindow(
                nullptr,
                { details.width, details.height },
                [this](WinEvent& event) { this->onEvent(event); }
            );

            group.push_back(this);
            
            this->unifiedConstructor();
        }

        void unifiedConstructor() {

            topLevelDetails = new TopLevelDetails();

            // Canvas
            //--------------------------------------------------

            topLevelDetails->canvas = new Canvas(window);
            event.canvas = topLevelDetails->canvas;

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

            // Final
            //--------------------------------------------------

            this->draw(event);

            this->draw(event);
        }

        // Destroy
        ~Window() {

            delete topLevelDetails->canvas;
            delete topLevelDetails;

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

            event.resetBeforeDispatch();
            topLevelDetails->dirtyElements.clear();

            this->calculateQueues();

            for (Element* element : topDown) { element->computeStyle(e); }
         
            this->calcFlexLayouts();

            for (Element* element : topDown) { element->computePrimitives(e); }

            topLevelDetails->dirtyElements.clear();

            topLevelDetails->canvas->beginFrame();

            for (Element* element : topDown) {
                if (element == this) { continue; }
                element->draw(e);
            }

            topLevelDetails->canvas->endFrame();

            /*if (e.causedRefresh) {
                draw(e);
            }*/
        }

        // Controlling window
        //--------------------------------------------------

        // Show window and focus
        void popUp() {

        }

        // Hide (iconify) window
        void minimize() {

        }

        void maximize() {
        }

        void setSize(int width, int height) {
            this->window->setSize(width, height);
        }

        void setPos(int x, int y) {

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

        virtual void onEvent(WinEvent& event) {

            switch (event.type) {

                case (WinEvent::Create): { this->onOpen(); break; }
                case (WinEvent::Destroy): { break; }
                case (WinEvent::Close): { this->onClose(event.rejected); break; }

                case (WinEvent::Focus): { this->onFocus(); break; }
                case (WinEvent::Defocus): { this->onDefocus(); break; }

                case (WinEvent::Move): { this->onMove(event.c, event.d); break; }
                case (WinEvent::Resize): { this->onResize(event.c, event.d); break; }
                case (WinEvent::Maximize): { this->onMaximize(); break; }
                case (WinEvent::Minimize): { this->onMinimize(); break; }
                case (WinEvent::Restore): { this->onRestore(); break; }

                case (WinEvent::Paint): { this->onRefresh(); break; }

                case (WinEvent::MouseMove): { this->onCursorPos(event.c, event.d); break; }
                case (WinEvent::MouseButton): { this->onMouseButton(event.a, event.b, event.c, event.d); break; }
                case (WinEvent::MouseWheel): { this->onMouseWheel(event.c, event.d); break; }

                case (WinEvent::Keyboard): { this->onKeyboard(event.a, event.b); break; }
                case (WinEvent::Character): { this->onCharacter(event.a); break; }
            }
        }

        // Overridable callbacks
        //--------------------------------------------------

        // When the content needs to be redrawn
        virtual void onRefresh() {

            //dbg("[Window] Refreshing");

            //this->draw(event);
        }

        void onOpen() {
            dbg("[Window] Open");
        }

        void onClose(bool& rejectClose) {
            dbg("[Window] Close");
            rejectClose = false;
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

            //dbg("[Window] Move: %i, %i", x, y);

            this->details.x = x;
            this->details.y = y;
        }

        // When the window is resized
        virtual void onResize(int width, int height) {

            //dbg("[Window] Resize: %i, %i", width, height);

            details.width = width;
            details.height = height;

            if (!topLevelDetails) { return; }
            if (!topLevelDetails->canvas) { return; }

            topLevelDetails->canvas->details.width = width;
            topLevelDetails->canvas->details.height = height;
            topLevelDetails->canvas->flags.resize = true;

            this->draw(event);
        }

        // Mouse/keyboard callbacks (window only)
        //--------------------------------------------------

        // When a mouse button is clicked or released
        void onMouseButton(int button, int action, int x, int y) {

            dbg("[Window] mouseButton");

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

        // When mouse wheel or trackpad scrolls
        void onMouseWheel(float dx, float dy) {

            dbg("[Window] mouseWheel: %i, %i", dx, dy);

            event.mouse.wheel = { dx, dy };
            event.resetBeforeDispatch();

            this->setTargets(event);

            this->mouseWheel(event);
            
            if (event.causedRefresh) {
                this->draw(event);
            }
        }

        // When a key is depressed or released
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