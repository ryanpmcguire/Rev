module;

#include <cmath>
#include <string>
#include <vector>
#include <ranges>
#include <functional>

export module Rev.Element;

import Rev.OpenGL.Canvas;

import Rev.Rect;
import Rev.Style;
import Rev.Computed;
import Rev.Event;

import Rev.Resolved;

export namespace Rev {

    struct Element {

        struct TopLevelDetails {
            //WebGpu::Surface* surface;
            std::vector<Element*> dirtyElements;
            Canvas* canvas = nullptr;
        };

        TopLevelDetails* topLevelDetails = nullptr;

        std::string name = "Element";
        Element* parent = nullptr;
        std::vector<Element*> children;
        
        Rect rect;

        StylePtr style;
        StylePtr hoverStyle;
        StylePtr dragStyle;

        std::vector<Style*> styles;
        
        Computed computed;
        bool dirty = true;

        // Create
        Element(Element* parent = nullptr, std::string name = "Element") {

            if (parent) {
                
                this->parent = parent;

                parent->children.push_back(this);
                topLevelDetails = parent->topLevelDetails;
                topLevelDetails->dirtyElements.push_back(this);
            }

            this->name = name;
        }
        
        // Destroy
        ~Element() {

            // Remove all children
            for (Element* child : children) { delete child; }
            children.clear();
        }

        // Computing
        //--------------------------------------------------

        std::vector<Transition> transitions;

        // Comptue style
        virtual void computeStyle(Event& e) {

            // Compile / apply styles
            //--------------------------------------------------

            Style old = computed.style;

            computed.style = Style();

            // Apply other styles, then own style
            computed.style.apply(styles);
            computed.style.apply(style);

            if (targetFlags.hover) { computed.style.apply(hoverStyle); }
            if (targetFlags.drag) { computed.style.apply(dragStyle); }

            // Create transitions if needed
            //--------------------------------------------------

            // If width has changed
            
            computed.style.animate(old, transitions, e.time);

            // Transition styles
            //--------------------------------------------------

            // Remove expired transitions
            transitions.erase(
                std::remove_if(transitions.begin(), transitions.end(), [&](const Transition& transition) {
                    return e.time > transition.endTime;
                }),
                transitions.end()
            );

            bool doRefresh = false;

            // Do transitions
            for (Transition& transition : transitions) {

                float& val = *transition.subject;

                if (e.time < transition.startTime) { continue; }

                if (e.time < transition.endTime) { 
                    float t = float((e.time - transition.startTime)) / float((transition.endTime - transition.startTime));
                    val = Transition::ease(transition.startVal, transition.endVal, t);
                }

                if (e.time > transition.endTime) {
                    val = transition.endVal;
                }   
                
                doRefresh = true;
            }

            if (doRefresh) { refresh(e); }
        }
        
        // Compute attributes
        virtual void computePrimitives(Event& e) {

            for (Element* child : children) {
                child->computePrimitives(e);
            }
        }

        virtual void draw(Event& e) {

        }

        // Layout
        //--------------------------------------------------

        struct Row {

            Rect rect;
            ResolvedSize size;
            std::vector<Element*> members;

            // Get number of growable dimensions
            int canGrow(Axis axis) {
               
                int numCanGrow = 0;

                for (Element* member : members) {
                    numCanGrow += member->res.canGrow(axis);
                }

                return numCanGrow;
            }
        };

        struct Layout {

            Rect rect;
            ResolvedSize size;
            std::vector<Row> rows;

            // Return number of rows that can grow along the axis
            int growableRows(Axis axis) {

                int count = 0;
                
                for (Row& row : rows) {
                    count += row.size.canGrow(axis);
                }

                return count;
            }
        };

        Layout layout;

        Layout* parentLayout = nullptr;
        Row* parentRow = nullptr;
        Resolved res;

        // An optional function to measure dimensions (useful for text)
        bool measure = false;
        virtual void measureDims(float maxWidth, float maxHeight) {}

        // TOP DOWN
        // Resolve dims that don't flex (px/pct)
        void resolveNonFlexDims() {

            // Reset resolved and rect
            res = Resolved();
            rect = Rect();

            // Get maximum inner width/height of parent
            float maxInnerWidth = parent->res.getMaxInner(Axis::Horizontal);
            float maxInnerHeight = parent->res.getMaxInner(Axis::Vertical);
            
            float minInnerWidth = parent->res.getMinInner(Axis::Horizontal);
            float minInnerHeight = parent->res.getMinInner(Axis::Vertical);

            Style& rStyle = computed.style;

            // Hack for window size
            if (this == parent) {
                maxInnerWidth = rStyle.size.width.val;
                maxInnerHeight = rStyle.size.height.val;
                minInnerWidth = rStyle.size.width.val;
                minInnerHeight = rStyle.size.height.val;
            }

            // Set dimensions abs or relative to maximum occupiable dims
            res.size.setNonFlex(rStyle.size, minInnerWidth, minInnerHeight, maxInnerWidth, maxInnerHeight);
            res.mar.setNonFlex(rStyle.margin, res.size);
            res.pad.setNonFlex(rStyle.padding, res.size);
            res.pos.setNonFlex(rStyle.position, res.size);

            // Set grow min/max, ignoring pos as that cannot grow
            res.size.setGrow(rStyle.size, res.size.w.max);
            res.mar.setGrow(rStyle.margin, maxInnerWidth);
            res.pad.setGrow(rStyle.padding, maxInnerWidth);

            // Inherit maxima from parent if none
            if (!res.size.w.max) { res.size.w.max = maxInnerWidth; }
            if (!res.size.h.max) { res.size.h.max = maxInnerHeight; }
        }

        // Resolve layout (bottom up)
        // This is by far the slowest function
        void resolveLayout() {

            // Reset layout
            layout = Layout();
            Row row = Row();

            // Wrap children
            //--------------------------------------------------

            // The maximum size this element can possibly contain
            float maxInnerWidth = res.getMaxInner(Axis::Horizontal);
            float maxInnerHeight = res.getMaxInner(Axis::Vertical);

            if (!measure) {

                float runningWidth = 0;
                float runningHeight = 0;

                for (Element* child : children) {

                    // The minimum size this element can possibly be
                    float minOuterWidth = child->res.getMinOuter(Axis::Horizontal);
                    float minOuterHeight = child->res.getMinOuter(Axis::Vertical);

                    // Should we create a new row, or are we add more?
                    // (In other words, have we exceeded the maximum inner width)
                    if (!row.members.empty() && runningWidth + minOuterWidth > maxInnerWidth) {
                        
                        // Push back, create new row
                        layout.rows.push_back(row);
                        row = Row();

                        // Reset running values
                        runningWidth = 0;
                        runningHeight = 0;
                    }

                    runningWidth += minOuterWidth;
                    runningHeight = std::max(runningHeight, minOuterHeight);

                    row.members.push_back(child);
                }

                // Add last row that didn't overflow
                layout.rows.push_back(row);
            }

            // If the element will be responsible for its own layout
            else {
                this->measureDims(maxInnerWidth, maxInnerHeight);
            }

            // Mark children as members of layout/row
            //--------------------------------------------------

            for (Row& row : layout.rows) {
                for (Element* member : row.members) {
                    member->parentLayout = &layout;
                    member->parentRow = &row;
                }
            }

            // Measure layout min/max
            //--------------------------------------------------

            for (Row& row : layout.rows) {

                for (Element* member : row.members) {

                    row.size.w.min += member->res.getMinOuter(Axis::Horizontal);
                    row.size.h.min = std::max(row.size.h.min, member->res.getMinOuter(Axis::Vertical));
                
                    row.size.w.max += member->res.getMaxOuter(Axis::Horizontal);
                    row.size.h.max = std::max(row.size.h.max, member->res.getMaxOuter(Axis::Vertical));
                }

                layout.size.w.min = std::max(layout.size.w.min, row.size.w.min);
                layout.size.h.min += row.size.h.min;

                layout.size.w.max = std::max(layout.size.w.max, row.size.w.max);
                layout.size.h.max += row.size.h.max;
            }

            // Inherit minima from children
            //--------------------------------------------------

            if(res.size.w.fit && layout.size.w.min + res.pad.l.min + res.pad.r.min > res.size.w.min) {
                res.size.w.min = layout.size.w.min + res.pad.l.min + res.pad.r.min;
                res.size.w.max = std::max(res.size.w.min, res.size.w.max);
            }

            if(res.size.h.fit && layout.size.h.min + res.pad.t.min + res.pad.b.min > res.size.h.min) {
                res.size.h.min = layout.size.h.min + res.pad.t.min + res.pad.b.min;
                res.size.h.max = std::max(res.size.h.min, res.size.h.max);
            }

            // Inherit maxima from children
            //--------------------------------------------------

            if (res.size.w.fit && !res.size.w.max) { res.size.w.max = layout.size.w.max + res.pad.l.val + res.pad.r.val; }
            if (res.size.h.fit && !res.size.h.max) { res.size.h.max = layout.size.h.max + res.pad.t.val + res.pad.b.val; }
        }

        // BOTTOM UP
        // Promote flex dimensions to allow parents to grow to accomodate children
        void promoteFlexDims() {

            // Promote growable
            //--------------------------------------------------

            bool outerWidthGrowable = (res.size.w.growable || res.mar.l.growable || res.mar.r.growable);
            bool outerHeightGrowable = (res.size.h.growable || res.mar.t.growable || res.mar.b.growable);

            if (outerWidthGrowable) {
                parent->res.size.w.growable = true;
            }

            if (outerHeightGrowable) {
                parent->res.size.h.growable = true;
                if (parentRow) { parentRow->size.h.growable = true; }
                if (parentLayout) { parentLayout->size.h.growable = true; }
            }

            // Reconsider maximum if layout has value
            //--------------------------------------------------

            bool test = true;
        }

        // Resolve grow/shrink
        void resolveFlexDims() {

            // Resolve own padding and position (relative)
            //--------------------------------------------------

            float width = res.size.w.val;
            float height = res.size.h.val;

            Style& rStyle = computed.style;

            // Padding
            if (rStyle.padding.left.type == Dist::Type::Rel) { res.pad.l.val = rStyle.padding.left.val * width; }
            if (rStyle.padding.right.type == Dist::Type::Rel) { res.pad.r.val = rStyle.padding.right.val * width; }
            if (rStyle.padding.top.type == Dist::Type::Rel) { res.pad.t.val = rStyle.padding.top.val * height; }
            if (rStyle.padding.bottom.type == Dist::Type::Rel) { res.pad.b.val = rStyle.padding.bottom.val * height; }

            // Position            
            if (rStyle.position.left.type == Dist::Type::Rel) { res.pos.l.val = rStyle.position.left.val * width; }
            if (rStyle.position.right.type == Dist::Type::Rel) { res.pos.r.val = rStyle.position.right.val * width; }
            if (rStyle.position.top.type == Dist::Type::Rel) { res.pos.t.val = rStyle.position.top.val * height; }
            if (rStyle.position.bottom.type == Dist::Type::Rel) { res.pos.b.val = rStyle.position.bottom.val * height; }

            // Requires children
            if (children.empty()) {
                return;
            }

            // Resolve size and margin of children prior to flex grow
            //--------------------------------------------------

            float innerWidth = res.getInner(Axis::Horizontal);
            float innerHeight = res.getInner(Axis::Vertical);

            for (Row& row : layout.rows) {
                
                row.size.clamp();

                for (Element* member : row.members) {
                    
                    Element& elem = *member;

                    Style& elemStyle = elem.computed.style;

                    // Resolve relative width
                    if (elemStyle.size.width.type == Dist::Type::Rel) {
                        elem.res.size.w.val = elemStyle.size.width.val * innerWidth;
                    }

                    // Resolve relative height
                    if (elemStyle.size.height.type == Dist::Type::Rel) {
                        elem.res.size.h.val = elemStyle.size.height.val * innerHeight;
                    }

                    // Margins are resolved by the element width
                    float elemWidth = elem.res.size.w.val;
                    float elemHeight = elem.res.size.h.val;

                    // Resolve margin
                    if (elemStyle.margin.left.type == Dist::Type::Rel) { elem.res.mar.l.val = elemStyle.margin.left.val * elemWidth; }
                    if (elemStyle.margin.right.type == Dist::Type::Rel) { elem.res.mar.r.val = elemStyle.margin.right.val * elemWidth; }
                    if (elemStyle.margin.top.type == Dist::Type::Rel) { elem.res.mar.t.val = elemStyle.margin.top.val * elemHeight; }
                    if (elemStyle.margin.bottom.type == Dist::Type::Rel) { elem.res.mar.b.val = elemStyle.margin.bottom.val * elemHeight; }
                }
            }

            // Clamp all in layout
            //--------------------------------------------------

            layout.size.clamp();

            for (Row& row : layout.rows) {
                
                row.size.clamp();

                for (Element* member : row.members) {
                    member->res.size.clamp();
                }
            }

            remeasureLayout();

            // Grow growable dimensions (horizontal)
            //--------------------------------------------------

            layout.size.w.max = std::min(layout.size.w.max, res.getInner(Axis::Horizontal));

            for (Row& row : layout.rows) {

                row.size.w.max = std::min(row.size.w.max, layout.size.w.max);
                float availableWidth = row.size.w.max - row.size.w.val;

                // Loop until break conditions are met
                while (true) {

                    int numGrowable = row.canGrow(Axis::Horizontal);
                    float share = availableWidth / float(numGrowable);

                    // When there's no more space or no more growable elements
                    if (!numGrowable || availableWidth < 0.01) {
                        break;
                    }

                    for (Element* member : row.members) {
                        float take = member->res.grow(share, Axis::Horizontal);
                        row.size.w.val += take;
                        availableWidth -= take;
                    }
                }
            }

            // Grow each row (vertical)
            //--------------------------------------------------

            // Consider moving back to "min" strategy to handle fitting
            layout.size.h.max = std::min(layout.size.h.max, res.getInner(Axis::Vertical));
            float availableHeight = layout.size.h.max - layout.size.h.val;

            // Loop until break conditions are met
            while (true) {

                int numGrowable = layout.growableRows(Axis::Vertical);
                float share = availableHeight / float(numGrowable);

                // When there's no more space or no more growable elements
                if (!numGrowable || availableHeight < 0.01) {
                    break;
                }

                for (Row& row : layout.rows) {
                    float take = row.size.h.grow(share);
                    layout.size.h.val += take;
                    availableHeight -= take;
                }
            }

            // Grow each row member (vertical)
            //--------------------------------------------------

            for (Row& row : layout.rows) {
                for (Element* member : row.members) {
                    
                    Element& elem = *member;

                    float availableElemHeight = row.size.h.val - elem.res.getOuter(Axis::Vertical);

                    while (true) {
                        
                        int numGrowable = elem.res.canGrow(Axis::Vertical);
                        float share = availableElemHeight / float(numGrowable);

                        if (!numGrowable || availableElemHeight < 0.01) {
                            break;
                        }
                        
                        float take = elem.res.grow(share, Axis::Vertical);
                        availableElemHeight -= take;
                    }
                }
            }
        }

        void remeasureLayout() {

            // Requires children
            if (children.empty()) {
                return;
            }

            layout.size.w.val = 0;
            layout.size.h.val = 0;

            for (Row& row : layout.rows) {
                
                row.size.w.val = 0;
                row.size.h.val = 0;

                for (Element* member : row.members) {
                    row.size.w.val += member->res.getOuter(Axis::Horizontal);
                    row.size.h.val = std::max(row.size.h.val, member->res.getOuter(Axis::Vertical));
                }

                layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                layout.size.h.val += row.size.h.val;
            }
        }

        // Reusable center function
        float center(float parent, float child, Align align) {

            switch (align) {
                case (Align::Start): { return 0; break; }
                case (Align::End): { return parent - child; break; }
                case (Align::Center): { return (parent - child) / 2; break; }
                case (Align::NoAlign): { return 0; break; }
            }
        }

        // Resolve final positions (top down)
        void resolveRects() {

            // If top level
            if (parent == this) {
                rect = {
                    0, 0,
                    res.size.w.val, res.size.h.val
                };
            }

            // Requires children
            if (children.empty()) {
                return;
            }

            // Resolve dimensions
            //--------------------------------------------------

            // Resolve layout dimensions
            layout.size.clamp();
            layout.rect.w = layout.size.w.val;
            layout.rect.h = layout.size.h.val;

            // Resolve row dimensions
            for (Row& row : layout.rows) {
                
                row.size.clamp();
                row.rect.w = row.size.w.val;
                row.rect.h = row.size.h.val;

                // Resolve member dimensions
                for (Element* member : row.members) {
                    member->res.size.clamp();
                    member->rect.w = member->res.size.w.val;
                    member->rect.h = member->res.size.h.val;
                }
            }

            // Resolve positions
            //--------------------------------------------------

            float layoutOffsetX = res.pad.l.val;
            float layoutOffsetY = res.pad.t.val;

            Style& rStyle = computed.style;

            layoutOffsetX += center(res.getInner(Axis::Horizontal), layout.rect.w, rStyle.alignment.horizontal);
            layoutOffsetY += center(res.getInner(Axis::Vertical), layout.rect.h, rStyle.alignment.vertical);

            // Resolve layout position
            layout.rect.x = rect.x + layoutOffsetX;
            layout.rect.y = rect.y + layoutOffsetY;

            float runningY = 0;

            // Position rows
            for (Row& row : layout.rows) {

                row.rect.x = layout.rect.x;
                row.rect.y = layout.rect.y + runningY;

                float runningX = 0;

                for (Element* member : row.members) {

                    member->rect.x = member->res.mar.l.val + row.rect.x + runningX;
                    member->rect.y = member->res.mar.t.val + row.rect.y;

                    runningX += member->rect.w + member->res.mar.l.val + member->res.mar.r.val;

                    // Apply relative positions
                    //--------------------------------------------------

                    if (member->res.pos.l.val != -0.0f) { member->rect.x += member->res.pos.l.val; }
                    if (member->res.pos.r.val != -0.0f) { member->rect.x += member->res.pos.r.val; }
                    if (member->res.pos.t.val != -0.0f) { member->rect.y += member->res.pos.t.val; }
                    if (member->res.pos.b.val != -0.0f) { member->rect.y += member->res.pos.b.val; }
                }

                runningY += row.rect.h;
            }
        }

        // Event callbacks
        //--------------------------------------------------------------------------------

        struct ListenerGroup {

            using ListenerFunc = void (Element::*)(Event&);
            std::vector<std::function<void(Event&)>> listeners;
            ListenerFunc func;

            ListenerGroup(ListenerFunc f) : func(f) {}
        };

        std::vector<ListenerGroup> listenerGroups;

        // Register a listener for a specific function (used for lookup)
        void listen(ListenerGroup::ListenerFunc func, const std::function<void(Event&)>& listener) {

            // Check if the function already has a listener group
            auto it = std::find_if(listenerGroups.begin(), listenerGroups.end(),
                [func](const ListenerGroup& group) {
                    return func == group.func;  // Compare function pointers (addresses)
                });

            // If found, add the listener to the group
            if (it != listenerGroups.end()) {
                it->listeners.push_back(listener);
            }

            // If not found, create a new group
            else {
                ListenerGroup newGroup(func);
                newGroup.listeners.push_back(listener);
                listenerGroups.push_back(newGroup);
            }
        }

        // "tell" function to notify listeners of a specific function
        void tell(ListenerGroup::ListenerFunc tellingFunc, Event& e) {

            // Search for the listener group that matches the telling function
            auto it = std::find_if(listenerGroups.begin(), listenerGroups.end(),
                [tellingFunc](const ListenerGroup& group) {
                    return tellingFunc == group.func;  // Compare function pointers (addresses)
                });

            // If a matching listener group is found, notify all its listeners
            if (it != listenerGroups.end()) {
                for (auto& listener : it->listeners) {
                    listener(e);
                }
            }
        }

        // Wrapper functions
        void onRefresh(const std::function<void(Event&)>& listener) { this->listen(&Element::refresh, listener); }
        void onMouseDown(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseDown, listener); }
        void onMouseUp(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseUp, listener); }
        void onMouseMove(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseMove, listener); }
        void onDrag(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseDrag, listener); }
        void onMouseEnter(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseEnter, listener); }
        void onMouseLeave(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseLeave, listener); }
        void onMouseWheel(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseWheel, listener); }
        void onKeyDown(const std::function<void(Event&)>& listener) { this->listen(&Element::keyDown, listener); }
        void onKeyUp(const std::function<void(Event&)>& listener) { this->listen(&Element::keyUp, listener); }

        // Event propagation
        //--------------------------------------------------

        struct TargetFlags {
            bool hit = false;
            bool click = false;
            bool hover = false;
            bool focus = false;
            bool drag = false;
        };

        TargetFlags targetFlags;

        virtual void refresh(Event& e) {

            // Set self
            this->dirty = true;
            e.causedRefresh = true;
            topLevelDetails->dirtyElements.push_back(this);

            // Propagate upwards
            if (parent && !parent->dirty) { parent->refresh(e); }
        }

        virtual void mouseDown(Event& e) {

            // Mouse down event means we are a drag target
            if (!targetFlags.drag) {
                targetFlags.drag = true;
                refresh(e);
            }

            // Tell event listeners
            tell(&Element::mouseDown, e);
            if (!e.propagate) { return; }

            // Propagate
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                // If child contains event, we propagate
                if (child.targetFlags.hit) {
                    child.mouseDown(e);
                }

                if (!e.propagate) {
                    return;
                }
            }
        }

        virtual void mouseUp(Event& e) {

            // Mouseup means dragging must end
            if (targetFlags.drag) {
                targetFlags.drag = false;
                refresh(e);
            }

            // Stop if listener does not pass "continue" flag
            tell(&Element::mouseUp, e);
            if (!e.propagate) { return; }

            // Process children in reverse
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                // If child contains the event or is/was a drag target
                if (child.targetFlags.hit || child.targetFlags.drag) {
                    child.mouseUp(e);
                }

                if (!e.propagate) { return; }
            }
        }

        // When the mouse moves on/over an element
        virtual void mouseMove(Event& e) {

            // If there is a cursor we need to set
            if (this->style->cursor != Cursor::Unset) {
                e.mouse.cursor = this->style->cursor;
            }

            // Stop if listener does not pass "continue" flag
            tell(&Element::mouseMove, e);
            if (!e.propagate) { return; }
            
            // Process children in reverse
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool isHoverTarget = child.targetFlags.hover;
                bool containsEvent = child.targetFlags.hit;

                if (containsEvent && !isHoverTarget) { child.mouseEnter(e); }
                if (!containsEvent && isHoverTarget) { child.mouseLeave(e); }
                if (containsEvent) { child.mouseMove(e); }

                if (!e.propagate) { return; }
            }
        }

        // When the mouse enters the element
        virtual void mouseEnter(Event& e) {

            if (!targetFlags.hover) {
                targetFlags.hover = true;
                refresh(e);
            }

            // Tell event listeners
            tell(&Element::mouseEnter, e);
            if (!e.propagate) { return; }

            // Propagate to children
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isHoverTarget = child.targetFlags.hover;

                if (containsEvent && !isHoverTarget) { child.mouseEnter(e); }
                if (!e.propagate) { return; }
            }
        }

        // When a mouse leaves an element
        virtual void mouseLeave(Event& e) {

            if (targetFlags.hover) {
                targetFlags.hover = false;
                refresh(e);
            }

            tell(&Element::mouseLeave, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isMouseOverTarget = child.targetFlags.hover;

                if (!containsEvent && isMouseOverTarget) { child.mouseLeave(e); }
                if (!e.propagate) { return; }
            }
        }

        virtual void mouseDrag(Event& e) {

            // Stop if listener does not pass "continue" flag
            tell(&Element::mouseDrag, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                // We propagate to all children which are also drag targets
                if (child.targetFlags.drag) { child.mouseDrag(e); }
                if (!e.propagate) { return; }
            }
        }

        virtual void mouseWheel(Event& e) {

            tell(&Element::mouseWheel, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                // Check if the e position is within the child's rectangle
                if (child.targetFlags.hit) { child.mouseWheel(e); }
                if (!e.propagate) { return; }
            }
        }

        virtual void keyDown(Event& e) {

            tell(&Element::keyDown, e);
            if (!e.propagate) { return; }

            // Propogate in reverse order
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                if (child.targetFlags.focus) { child.keyDown(e); }
                if (!e.propagate) { return; }
            }
        }

        virtual void keyUp(Event& e) {

            tell(&Element::keyUp, e);
            if (!e.propagate) { return; }

            // Propagate in reverse order
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                if (child.targetFlags.focus) { child.keyUp(e); }
                if (!e.propagate) { return; }
            }
        }

        // Collision / Containment / Intersection
        //--------------------------------------------------------------------------------

        bool includeChildren = false;

        // Default behavior is to ask our rect if it contains a position
        virtual bool contains(Pos& pos) {
        
            if (includeChildren) {
                return (rect.contains(pos) || anyChildContains(pos));
            }
            
            return this->rect.contains(pos);
        }

        virtual bool intersects(Rect& rect) {
            return this->rect.intersects(rect);
        }

        // Sometimes we need to skip and simply pass the concern to our children
        bool anyChildContains(Pos& pos) {

            for (Element* child: children) {
                if (child->contains(pos)) { return true; }
            }

            return false;
        }

        bool anyChildIntersects(Rect& rect) {

            for (Element* child: children) {
                if (child->intersects(rect)) {
                    return true;
                }
            }

            return false;
        }

        // Sometimes we need to skip multiple levels and simply ask whether ANY decendent contains a position
        bool decendantContains(Pos& pos) {
            
            for (Element* child : children) {

                if (child->contains(pos)) { return true; }
                if (child->decendantContains(pos)) { return true; }
            }

            return false;
        }

        // Sometimes we need to skip multiple levels and simply ask whether ANY decendent contains a position
        bool decendantIntersects(Rect& rect) {
            
            for (Element* child : children) {

                if (child->intersects(rect)) { return true; }
                if (child->decendantIntersects(rect)) { return true; }
            }

            return false;
        }
    };
}