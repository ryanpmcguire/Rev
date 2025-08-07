module;

#include <vector>
#include <string>
#include <GLFW/glfw3.h>
#include <dbg.hpp>

export module Rev.Window;

import Rev.Element;
import Rev.Box;
import Rev.OpenGL.Canvas;

export namespace Rev {

    struct Window : public Element {

        struct Details {

            std::string name = "Hello World";
            
            int width = 640, height = 480;
            int x = 0, y = 0;

            bool decorated = true;
            bool resizable = true;
        };

        // Persistent event
        Event event;

        // Glfw
        GLFWwindow* window = nullptr;
        Details details;

        bool shouldClose = false;

        int xPin = 0; int yPin = 0;
        Pos downPos = { 0, 0 };

        // Create
        Window(std::vector<Window*>& group, Details details = {}) {

            this->parent = this;
            this->details = details;

            topLevelDetails = new TopLevelDetails();

            // GLFW
            //--------------------------------------------------

            // Create window
            //glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

            // Set OpenGL context hints
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            //glfwWindowHint(GLFW_SAMPLES, 8); // 4x MSAA

            glfwWindowHint(GLFW_DECORATED, details.decorated ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, details.resizable ? GLFW_TRUE : GLFW_FALSE);

            window = glfwCreateWindow(details.width, details.height, details.name.c_str(), nullptr, nullptr);
            glfwSetWindowUserPointer(window, this);

            // Window callbacks
            glfwSetFramebufferSizeCallback(window, handleFramebufferResize);
            glfwSetWindowRefreshCallback(window, handleRefresh);
            glfwSetWindowContentScaleCallback(window, handleContentScale);
            glfwSetWindowFocusCallback(window, handleFocus);
            glfwSetWindowPosCallback(window, handleMove);
            glfwSetWindowSizeCallback(window, handleResize);
            glfwSetWindowIconifyCallback(window, handleMinimize);
            glfwSetWindowMaximizeCallback(window, handleMaximize);
            glfwSetWindowCloseCallback(window, handleClose);

            // Mouse / keyboard callbacks
            glfwSetMouseButtonCallback(window, handleMouseButton);
            glfwSetCursorPosCallback(window, handleCursorPos);

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

            glfwDestroyWindow(window);
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

            //dbg("Drawing");

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
        }

        // Overridable callbacks
        //--------------------------------------------------

        // When the framebuffer resizes
        virtual void onFramebufferResize(int width, int height) {
            //dbg("Framebuffer: (%i, %i)", width, height);
        }

        // When the content needs to be redrawn
        virtual void onRefresh() {
            //dbg("Refresh");

            this->draw(event);
        }

        // When the content scale changes
        virtual void onContentScale(float xscale, float yscale) {
            //dbg("Content scale: (%f, %f)", xscale, yscale);
        }

        // When the window gains or loses focus
        virtual void onFocusChange(int focused) {
            if (focused) { this->onFocus(); }
            else { this->onDefocus(); }
        }

        // When the window gains focus
        virtual void onFocus() {
            //dbg("Focus");
        }

        // When the window loses focus
        virtual void onDefocus() {
            //dbg("Defocus");
        }

        // When the window changes position
        virtual void onMove(int x, int y) {
            //dbg("Move: (%i, %i)", x, y);
            this->details.x = x;
            this->details.y = y;
        }

        // When the window is resized
        virtual void onResize(int width, int height) {

            details.width = width;
            details.height = height;

            topLevelDetails->canvas->flags.resize = true;
        }

        // When the window is maximized
        virtual void onMaximizeChange(int maximized) {
            if (maximized) { this->onMaximize(); }
            else { this->onDemaximize(); }
        }

        virtual void onMaximize() {
            //dbg("Maximize");
        }

        virtual void onDemaximize() {
            //dbg("Demaximize");
        }

        // When the window is minimized
        virtual void onMinimizeChange(int minimized) {
            if (minimized) { this->onMinimize(); }
            else { this->onDeminimize(); }
        }

        virtual void onMinimize() {
            //dbg("Minimize");
        }
        
        virtual void onDeminimize() {
            //dbg("Deminimize");
        }

        void onClose() {
            //dbg("Close");
            shouldClose = true;
        }

        // Mouse/keyboard callbacks (window only)
        //--------------------------------------------------

        enum ButtonAction {
            Press = GLFW_PRESS,
            Release = GLFW_RELEASE
        };

        enum MouseButton {
            Left = GLFW_MOUSE_BUTTON_LEFT,
            Right = GLFW_MOUSE_BUTTON_RIGHT,
            Middle = GLFW_MOUSE_BUTTON_MIDDLE,
            Button4 = GLFW_MOUSE_BUTTON_4,
            Button5 = GLFW_MOUSE_BUTTON_5,
            Button6 = GLFW_MOUSE_BUTTON_6,
            Button7 = GLFW_MOUSE_BUTTON_7,
            Button8 = GLFW_MOUSE_BUTTON_8
        };

        // Bubble-up hit status
        void setTargets(Event& e) {

            for (Element* element : topDown) {
                element->targetFlags.hit = false;
            }

            for (Element* element : bottomUp) {
                
                Element& elem = *element;

                //
                if (elem.contains(e.mouse.pos)) { elem.targetFlags.hit = true; }
                if (elem.targetFlags.hit) { elem.parent->targetFlags.hit = true; }
            }
        }

        // When a mouse button is clicked or released
        void onMouseButton(int button, int action) {

            // Get mouse position
            double x, y; glfwGetCursorPos(window, &x, &y);
            event.mouse.pos.x = float(x); event.mouse.pos.y = float(y);

            event.resetBeforeDispatch();
            event.id += 1;

            this->setTargets(event);

            switch (button) {
                case (MouseButton::Left): { event.mouse.lb.set(action, event.mouse.pos); break; }
                case (MouseButton::Right): { event.mouse.rb.set(action, event.mouse.pos); break; }
            }

            if (action == ButtonAction::Press) { this->mouseDown(event); }
            if (action == ButtonAction::Release) { this->mouseUp(event); }

            if (event.causedRefresh) {
                this->draw(event);
            }
        }

        // When the mouse moves
        void onCursorPos(float x, float y) {

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

        // Controlling window
        //--------------------------------------------------

        // Show window and focus
        void popUp() {
            glfwShowWindow(window);
            glfwFocusWindow(window);
        }

        // Hide (iconify) window
        void minimize() {
            glfwIconifyWindow(window);
        }

        void maximize() {
            glfwMaximizeWindow(window);
        }

        void setSize(int width, int height) {
            glfwSetWindowSize(window, width, height);
        }

        void setPos(int x, int y) {
            glfwSetWindowPos(window, x, y);
        }

        // Static callbacks for GLFW
        //--------------------------------------------------

        static Window* self(GLFWwindow* win) {
            return static_cast<Window*>(glfwGetWindowUserPointer(win));
        }

        // Window callbacks
        static void handleFramebufferResize(GLFWwindow* win, int width, int height) { self(win)->onFramebufferResize(width, height); }
        static void handleRefresh(GLFWwindow* win) { self(win)->onRefresh(); }
        static void handleContentScale(GLFWwindow* win, float xscale, float yscale) { self(win)->onContentScale(xscale, yscale);}
        static void handleFocus(GLFWwindow* win, int focused) { self(win)->onFocusChange(focused); }
        static void handleMove(GLFWwindow* win, int x, int y) { self(win)->onMove(x, y); }
        static void handleResize(GLFWwindow* win, int width, int height) { self(win)->onResize(width, height); }
        static void handleMaximize(GLFWwindow* win, int maximized) { self(win)->onMaximizeChange(maximized); }
        static void handleMinimize(GLFWwindow* win, int minimized) { self(win)->onMinimizeChange(minimized); }
        static void handleClose(GLFWwindow* win) { self(win)->onClose(); }

        // Mouse / keyboard callbacks
        static void handleMouseButton(GLFWwindow* win, int button, int action, int mods) { self(win)->onMouseButton(button, action); }
        static void handleCursorPos(GLFWwindow* win, double xpos, double ypos) { self(win)->onCursorPos(static_cast<float>(xpos), static_cast<float>(ypos)); }
    };
}