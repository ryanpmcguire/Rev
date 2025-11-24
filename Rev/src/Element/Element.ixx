module;

#include <cmath>
#include <string>
#include <vector>
#include <ranges>
#include <functional>

#include <sentinel.hpp>
#include <dbg.hpp>

export module Rev.Element;

import Rev.Graphics.Canvas;

import Rev.Core.Pos;
import Rev.Core.Rect;
import Rev.Core.DirtyFlag;

import Rev.Element.Style;
import Rev.Element.Event;
import Rev.Element.Resolved;

export namespace Rev::Element {

    using namespace sentinel;

    struct Element {

        enum class Type {
            Single,
            Group
        };

        Type type;
        std::vector<Element*> children;

        struct Shared {

            struct DirtyElements {
                std::vector<Element*> refresh;
                std::vector<Element*> restyle;
                std::vector<Element*> animate;
            };

            DirtyElements dirty;
            
            Graphics::Canvas* canvas = nullptr;
            std::vector<Element*> stencilStack;

            Event* event = nullptr;
        };

        struct Dirty {
            Core::DirtyFlag style;
            bool structure = false;
            bool draw = false;
        };

        // Shared betweeen elements
        Shared* shared = nullptr;

        // Self and parent
        Element* parent = nullptr;
        Element* next = nullptr;
        Element* last = nullptr;

        std::string name = "Element";
        
        // Style
        StylePtr style;
        StyleList styles;
        
        // Computing
        Resolved resolved;
        Rect rect;

        // Tracking
        size_t draws = 0;

        Dirty dirty;

        // Create
        Element(Element* parent = nullptr, StyleList styles = {}, std::string name = "") {

            if (parent && parent != this) { parent->addChild(this); }

            dirty.style.drawsFrom(&(this->styles.dirty));
            dirty.style.drawsFrom(&(this->style.dirty));

            dirty.style.onDirty([this]() {

                if (!this->shared) { return; }
                //if (this->parent == this) { return; }

                this->shared->dirty.restyle.push_back(this);
                this->refresh(*shared->event);
            });

            dirty.style = true;

            this->styles = styles;
            this->name = name;
        }
        
        // Destroy
        virtual ~Element() {

            // Before doing anything, remove self from parent
            parent->removeChild(this);

            if (style.pStyle) { delete style.pStyle; style.pStyle = nullptr; }

            // Delete children (from copy)
            std::vector<Element*> childrenCopy = children;
            for (Element* child : childrenCopy) { if (child) { delete child; } }
            children.clear();
        }

        // Cast as pointer to canvas
        explicit operator Graphics::Canvas*() {
            return shared ? shared->canvas : nullptr;
        }

        // Inheritance
        //--------------------------------------------------
        
        void addChild(Element* child) {

            child->parent = this;
            child->shared = shared;

            children.push_back(child);

            child->refresh(*shared->event);
        }

        void removeChild(Element* child) {
            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end()) { children.erase(it); }
        }

        void cascadeStyle() {

            // Set hidden state
            resolved.hidden = false;
            if (parent->resolved.hidden) { resolved.hidden = true; }
            if (resolved.style.visibility == Visibility::Hidden) { resolved.hidden = true; }

            // Set depth
            resolved.depth = parent->resolved.depth + 1 - resolved.style.zIndex;
            

            // Continue
            for (Element* child : children) {
                child->cascadeStyle();
            }
        }

        // Computing
        //--------------------------------------------------

        std::vector<Transition> transitions;

        void transition(float* val, float newVal, int ms) {

            if (*val == newVal) { return; }

            float old = *val;
            *val = newVal;

            bool hadTransitions = !transitions.empty();

            Transition::createNew(*val, old, transitions, shared->event->time, ms);

            if (!hadTransitions && !transitions.empty()) {
                shared->dirty.animate.push_back(this);
            }
        }

        virtual void animate(Event& e) {

            // Transition styles
            //--------------------------------------------------

            // Remove expired transitions
            transitions.erase(
                std::remove_if(transitions.begin(), transitions.end(), [&](const Transition& transition) {
                    return e.time > transition.endTime;
                }),
                transitions.end()
            );

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
            }
        }

        // Comptue style
        virtual void resolveStyle(Event& e) {

            resolved.hasHoverStyle = false;
            resolved.hasPressStyle = false;
            resolved.hasDragStyle = false;
            resolved.hasFocusStyle = false;
            resolved.hasDisabledStyle = false;

            // Calculate whether we have certain styles
            for (Style* style : styles.styles) {
                if (style->applies.hover) { resolved.hasHoverStyle = true; }
                if (style->applies.press) { resolved.hasPressStyle = true; }
                if (style->applies.drag) { resolved.hasDragStyle = true; }
                if (style->applies.focus) { resolved.hasFocusStyle = true; }
                if (style->applies.disabled) { resolved.hasDisabledStyle = true; }
            }

            // Compile / apply styles
            //--------------------------------------------------

            Style old = resolved.style;

            resolved.style = Style();
            resolved.style.dirty = false;

            Applies flags = {
                .hover = targetFlags.hover,
                .press = targetFlags.press,
                .drag = targetFlags.drag,
                .focus = targetFlags.focus,
                .disabled = resolved.disabled
            };

            // Apply other styles, then own style
            resolved.style.apply(styles, flags);
            if (style.pStyle) { resolved.style.apply(*(style.pStyle)); }

            // Set all styles as not dirty (anymore)
            if (style.pStyle) { style.pStyle->dirty = false; }
            for (Style* style : styles.styles) { style->dirty = false; }
            styles.dirty = false;

            this->dirty.style = false;

            // Create transitions if needed
            //--------------------------------------------------
            
            // If this is our first draw, we do not animate
            if (draws == 0) { return; }

            bool hadTransitions = !transitions.empty();
            resolved.style.animate(old, transitions, e.time);

            // Add to "please animate" list if we now have transitions
            if (!hadTransitions && !transitions.empty()) {
                shared->dirty.animate.push_back(this);
            }
        }

        // Alter children to reflect data
        virtual void computeChildren(Event& e) {}
        
        // Alter style to reflect data
        virtual void computeStyle(Event& e) {}

        // Alter primitives to reflect style/data
        virtual void computePrimitives(Event& e) {}

        // Draw stencil (this must be seperate from draw logic)
        virtual void stencil(Event& e) {}

        // Draw color
        virtual void draw(Event& e) {

            // Draw = no longer dirty
            this->dirty.draw = false;
            draws += 1;

            // If there are any incomplte transitions, we must continue drawing
            if (!transitions.empty()) {
                refresh(e);
            }
        }

        // Layout
        //--------------------------------------------------

        /* 
            ////////// WARNING ////////// WARNING ////////// WARNING //////////
        
            THIS CODE IS NOT ELEGANT. IT IS NOT PERFORMANT. IT IS FUCKED.
            IT IS VERY BAD. IT IS FULL OF CLUMSY BAND-AIDS AND REDUNDANT
            CALCULATIONS. IT WILL BE FIXED OVER TIME, BUT IT IS NECESSARY
            THAT IS SHOULD AT LEAST WORK, FOR THE TIME BEING.

            ////////// WARNING ////////// WARNING ////////// WARNING //////////
        */

        struct Row {

            Rect rect;
            ResolvedSize size;
            std::vector<Element*> members;

            // Get number of growable dimensions
            int canGrow(Axis axis) {
               
                int numCanGrow = 0;

                for (Element* member : members) {
                    if (!member->resolved.affectsParentSize) { continue; }
                    numCanGrow += member->resolved.canGrow(axis);
                }

                return numCanGrow;
            }
        };

        struct Layout {

            Rect rect;
            ResolvedSize size;
            std::vector<Row> rows;
            bool done = true;

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

        // Default is to simply reset the layout
        virtual void computeLayout() {
            layout = Layout();
            layout.done = false;
        }

        // Step zero is to reset all data which will be modified
        void resetResolved() {

            resolved.reset();

            rect = Rect();
        }

        void resolveMinima() {

            if (resolved.hidden) { return; }

            float& minWidth = resolved.min.width;
            float& minHeight = resolved.min.height;

            float& minPaddingWidth = resolved.min.paddingWidth;
            float& minPaddingHeight = resolved.min.paddingHeight;
            
            float& minMarginWidth = resolved.min.marginWidth;
            float& minMarginHeight = resolved.min.marginHeight;

            float& minInnerWidth = resolved.min.innerWidth;
            float& minInnerHeight = resolved.min.innerHeight;

            float& minOuterWidth = resolved.min.outerWidth;
            float& minOuterHeight = resolved.min.outerHeight;

            // Get from style
            //--------------------------------------------------

            minWidth = resolved.getMinSize(Axis::Horizontal, Dist::Type::Abs);
            minHeight = resolved.getMinSize(Axis::Vertical, Dist::Type::Abs);

            minPaddingWidth = resolved.getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            minPaddingHeight = resolved.getMinPadding(Axis::Vertical, Dist::Type::Abs);

            minMarginWidth = resolved.getMinMargin(Axis::Horizontal, Dist::Type::Abs);
            minMarginHeight = resolved.getMinMargin(Axis::Vertical, Dist::Type::Abs);

            // Infer from self (or children if necessary)
            //--------------------------------------------------

            if (set(minWidth)) {
                minInnerWidth = minWidth - minPaddingWidth;
                minOuterWidth = minWidth + minMarginWidth;
            }

            else {

                float maxOfMin = -0.0f;

                // Get max, ignoring if absolute
                for (Element* c : children) {
                    if (!c->resolved.affectsParentSize) { continue; }
                    maxOfMin = std::max(maxOfMin, c->resolved.min.outerWidth);
                }
                
                minInnerWidth = maxOfMin;
                minOuterWidth = maxOfMin + minPaddingWidth + minMarginWidth;
            }

            if (set(minHeight)) {
                minInnerHeight = minHeight - minPaddingHeight;
                minOuterHeight = minHeight + minMarginHeight;
            }

            else {

                float maxOfMin = -0.0f;

                // Get max, ignoring if absolute
                for (Element* c : children) {
                    if (!c->resolved.affectsParentSize) { continue; }
                    maxOfMin = std::max(maxOfMin, c->resolved.min.outerHeight);
                }

                minInnerHeight = maxOfMin;
                minOuterHeight = maxOfMin + minPaddingHeight + minMarginHeight;
            }
        }

        // Top down: resolve maximum feasible dimensions
        void resolveMaxima() {

            float& maxWidth = resolved.max.width;
            float& maxHeight = resolved.max.height;
            
            float& maxInnerWidth = resolved.max.innerWidth;
            float& maxInnerHeight = resolved.max.innerHeight;

            float& minInnerWidth = resolved.min.innerWidth;
            float& minInnerHeight = resolved.min.innerHeight;

            float& minPaddingWidth = resolved.min.paddingWidth;
            float& minPaddingHeight = resolved.min.paddingHeight;

            float& minMarginWidth = resolved.min.marginWidth;
            float& minMarginHeight = resolved.min.marginHeight;

            if (resolved.hidden) { return; }
        
            // Get from style
            //--------------------------------------------------
            
            maxWidth = resolved.getMaxSize(Axis::Horizontal, Dist::Type::Abs);
            maxHeight = resolved.getMaxSize(Axis::Vertical, Dist::Type::Abs);

            // Infer from set values
            //--------------------------------------------------

            if (set(maxWidth)) { maxInnerWidth = maxWidth - minPaddingWidth; }
            else { maxInnerWidth = parent->resolved.max.innerWidth - minMarginWidth - minPaddingWidth; }

            if (set(maxHeight)) { maxInnerHeight = maxHeight - minPaddingHeight; }
            else { maxInnerHeight = parent->resolved.max.innerHeight - minMarginHeight - minPaddingHeight; }

            // Subtract subling outer heights if no set height
            if (!set(maxHeight)) {
                /*for (Element* s : parent->children) {
                    if (s == this) { continue; }
                    //maxInnerHeight -= s->minOuterHeight;
                }*/
            }

            // Ensure minimum dominates (in certain circumstances)
            if (maxInnerWidth < minInnerWidth) { maxInnerWidth = minInnerWidth; }
            if (maxInnerHeight < minInnerHeight) { maxInnerHeight = minInnerHeight; }
        }

        void resolveLayout() {

            if (resolved.hidden) { return; }

            // Wrap children
            //--------------------------------------------------

            // If there are no children, we defer to our compute layout function
            if (children.empty()) {
                this->computeLayout();
            }

            // Otherwise, we compute the layout ourselves
            else {

                layout = Layout();
                Row row = Row();
                float runningRelSize = 0.0f;

                for (Element* child : children) {

                    Element& elem = *child;

                    // Ignore wrap entirely if element does not wrap
                    if (!elem.resolved.wrap || resolved.style.layout.wrap == Wrap::False) {
                        row.members.push_back(child);
                        continue;
                    }
                    
                    bool horizontal = (resolved.style.layout.direction != Axis::Vertical);
                    Axis axis = horizontal ? Axis::Horizontal : Axis::Vertical;

                    // Get minimum abs/rel sizes
                    //--------------------------------------------------

                    float& maxAbsInnerSize = horizontal ? resolved.max.innerWidth : resolved.max.innerHeight;
                    float& minAbsLayoutSize = horizontal ? elem.layout.size.w.min : elem.layout.size.h.min;

                    // These absolute minima were already calculated before
                    float& minAbsSize = horizontal ? elem.resolved.min.width : elem.resolved.min.height;
                    float& minAbsMargin = horizontal ? elem.resolved.min.marginWidth : elem.resolved.min.marginHeight;
                    float& minAbsPadding = horizontal ? elem.resolved.min.paddingWidth : elem.resolved.min.paddingHeight;

                    // We directly get the proportional values
                    float minRelSize = elem.resolved.getMinSize(axis, Dist::Type::Rel);
                    float minRelMargin = elem.resolved.getMinMargin(axis, Dist::Type::Rel);
                    float minRelPadding = elem.resolved.getMinPadding(axis, Dist::Type::Rel); 

                    // Calculate additional relative sizes
                    //--------------------------------------------------

                    float inverseAbsMaxInner = 1.0f / maxAbsInnerSize;

                    if (set(minAbsSize)) { minRelSize += minAbsSize * inverseAbsMaxInner; }
                    else { minRelSize += (minAbsLayoutSize + minAbsPadding) * inverseAbsMaxInner; }
                    if (set(minAbsMargin)) { minRelMargin += minAbsMargin * inverseAbsMaxInner; }

                    float minRelOuterSize = -0.0f;
                    float minRelLayoutSize = minAbsLayoutSize * inverseAbsMaxInner;
                    
                    minRelOuterSize = minRelLayoutSize + minRelPadding;
                    if (set(minRelSize)) { minRelOuterSize = minRelSize ; }
                    minRelOuterSize += minRelMargin;

                    // Should we add another row (wrap) or should we keep adding more?
                    // We must wrap if we have exceeded the maximum allowed inner space,
                    // but not if this would be the first member of the row
                    if (runningRelSize + minRelOuterSize > 1.0 && !row.members.empty()) {
                        layout.rows.push_back(row);
                        row = Row();
                        runningRelSize = 0;
                    }

                    // Add element to row, add min outer size to running relative space
                    row.members.push_back(child);
                    runningRelSize += minRelOuterSize;
                }

                // Add last row that didn't overflow
                layout.rows.push_back(row);
            }

            // Measure layout val/min
            //--------------------------------------------------

            if (resolved.style.layout.direction == Axis::Vertical) {
            
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }
    
                        row.size.w.min = std::max(row.size.w.min, member->resolved.min.outerWidth);
                        row.size.h.min += member->resolved.min.outerHeight;
                    }

                    layout.size.w.min += row.size.w.min;
                    layout.size.h.min = std::max(layout.size.h.min, row.size.h.min);
                }
            }

            else {

                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }
    
                        row.size.w.min += member->resolved.min.outerWidth;
                        row.size.h.min = std::max(row.size.h.min, member->resolved.min.outerHeight);
                    }
    
                    layout.size.w.min = std::max(layout.size.w.min, row.size.w.min);
                    layout.size.h.min += row.size.h.min;
                }
            }

            // Adjust own minimum outer size to accomodate layout
            //--------------------------------------------------

            if (set(resolved.min.width)) { resolved.min.outerWidth = resolved.min.width + resolved.min.marginWidth; }
            else { resolved.min.outerWidth = layout.size.w.min + resolved.min.paddingWidth + resolved.min.paddingHeight; }

            if (set(resolved.min.height)) { resolved.min.outerHeight = resolved.min.height + resolved.min.marginHeight; }
            else { resolved.min.outerHeight = layout.size.h.min + resolved.min.paddingHeight + resolved.min.marginHeight; }

            float layoutPlusPaddingWidth = layout.size.w.min + resolved.style.padding.left.val + resolved.style.padding.right.val;
            float layoutPlusPaddingHeight = layout.size.h.min + resolved.style.padding.top.val + resolved.style.padding.bottom.val;

            Resolved& res = resolved;

            if (!set(res.size.w.min) && res.size.w.min < layoutPlusPaddingWidth) { res.size.w.min = layoutPlusPaddingWidth; }
            if (!set(res.size.h.min) && res.size.h.min < layoutPlusPaddingHeight) { res.size.h.min = layoutPlusPaddingHeight; }

            bool test = true;

            // Promote growable dims
            //--------------------------------------------------

            for (Row& row : layout.rows) {

                for (Element* member : row.members) {

                    Element& elem = *member;
                    Size& size = elem.resolved.style.size;

                    // Set dimensions as growable if style size is of type grow
                    if (size.width.type == Dist::Type::Grow) { elem.resolved.size.w.growable = true; }
                    if (size.height.type == Dist::Type::Grow) { elem.resolved.size.h.growable = true; }

                    if (!elem.resolved.affectsParentSize) {
                        continue;
                    }

                    if (resolved.style.layout.direction == Axis::Vertical) {
                    
                        if (elem.resolved.canGrow(Axis::Vertical)) {
                            resolved.size.h.growable = true;
                        }
    
                        if (elem.resolved.canGrow(Axis::Horizontal)) {
                            resolved.size.w.growable = true;
                            row.size.w.growable = true;
                            layout.size.w.growable = true;
                        }
                    }

                    else {
                        if (elem.resolved.canGrow(Axis::Horizontal)) {
                            resolved.size.w.growable = true;
                        }
    
                        if (elem.resolved.canGrow(Axis::Vertical)) {
                            resolved.size.h.growable = true;
                            row.size.h.growable = true;
                            layout.size.h.growable = true;
                        }
                    }
                }
            }
        }

        // Top down: Resolve flex and grow dimensions
        void resolveDims() {

            resolved.innerWidth = 0;
            resolved.innerHeight = 0;

            if (resolved.hidden) { return; }

            Resolved& res = resolved;
            
            if (parent == this) {
                res.size.w.val = res.size.w.min = res.size.w.max = resolved.style.size.width.val;
                res.size.h.val = res.size.h.min = res.size.h.max = resolved.style.size.height.val;
            }

            resolved.innerWidth = res.size.w.val;
            resolved.innerHeight = res.size.h.val;

            // Resolve own padding (needed for fit)
            //--------------------------------------------------
            
            res.pad.l.val = res.pad.l.min = res.pad.l.max = resolved.style.padding.left.val;
            res.pad.r.val = res.pad.r.min = res.pad.r.max = resolved.style.padding.right.val;
            res.pad.t.val = res.pad.t.min = res.pad.t.max = resolved.style.padding.top.val;
            res.pad.b.val = res.pad.b.min = res.pad.b.max = resolved.style.padding.bottom.val;

            resolved.innerWidth -= res.pad.l.val + res.pad.r.val;
            resolved.innerHeight -= res.pad.t.val + res.pad.b.val;

            if (children.empty()) {
                return;
            }

            // Resolve val/max of children prior to grow
            //--------------------------------------------------

            for (Element* pChild : children) {

                Element& child = *pChild;

                Size& cSize = child.resolved.style.size;

                Dist& cWidth = cSize.width;
                Dist& cHeight = cSize.height;

                LrtbStyle& cMargin = child.resolved.style.margin;
                LrtbStyle& cPadding = child.resolved.style.padding;
                LrtbStyle& cPosition = child.resolved.style.position;

                float& compareValW = child.resolved.style.layout.position == Position::Absolute ? resolved.size.w.val : resolved.innerWidth;
                float& compareValH = child.resolved.style.layout.position == Position::Absolute ? resolved.size.h.val : resolved.innerHeight;

                // Resolve nominal
                if (cSize.width) { child.resolved.size.w.val = child.resolved.size.w.min = child.resolved.size.w.max = cSize.width.resolve(compareValW); }
                if (cSize.height) { child.resolved.size.h.val = child.resolved.size.h.min = child.resolved.size.h.max = cSize.height.resolve(compareValH); }

                // Resolve min
                if (cSize.min.width) { child.resolved.size.w.min = cSize.min.width.resolve(compareValW); }
                if (cSize.min.height) { child.resolved.size.h.min = cSize.min.height.resolve(compareValH); }

                // Resolve max
                if (cSize.max.width) { child.resolved.size.w.max = cSize.max.width.resolve(compareValW); }
                if (cSize.max.height) { child.resolved.size.h.max = cSize.max.height.resolve(compareValH); }

                // Override max if needed
                if (cSize.width.type == Dist::Type::Grow && !set(child.resolved.size.w.max)) { child.resolved.size.w.max = 9999999.0f; }
                if (cSize.height.type == Dist::Type::Grow && !set(child.resolved.size.h.max)) { child.resolved.size.h.max = 9999999.0f; }

                // Resolve child margin
                child.resolved.mar.l.val = child.resolved.mar.l.min = child.resolved.mar.l.max = cMargin.left.val;
                child.resolved.mar.r.val = child.resolved.mar.r.min = child.resolved.mar.r.max = cMargin.right.val;
                child.resolved.mar.t.val = child.resolved.mar.t.min = child.resolved.mar.t.max = cMargin.top.val;
                child.resolved.mar.b.val = child.resolved.mar.b.min = child.resolved.mar.b.max = cMargin.bottom.val;

                // Resolve child padding
                child.resolved.pad.l.val = child.resolved.pad.l.min = child.resolved.pad.l.max = cPadding.left.val;
                child.resolved.pad.r.val = child.resolved.pad.r.min = child.resolved.pad.r.max = cPadding.right.val;
                child.resolved.pad.t.val = child.resolved.pad.t.min = child.resolved.pad.t.max = cPadding.top.val;
                child.resolved.pad.b.val = child.resolved.pad.b.min = child.resolved.pad.b.max = cPadding.bottom.val;

                // Resolve child position
                if (cPosition.left) {
                    child.resolved.pos.l.val = cPosition.left.resolve(compareValW);
                }
                if (cPosition.top) { child.resolved.pos.t.val = cPosition.top.resolve(compareValH); }

                float minWidthFromPadding = child.resolved.pad.l.min + child.resolved.pad.r.min;
                float minHeightFromPadding = child.resolved.pad.t.min + child.resolved.pad.b.min;

                if (!set(child.resolved.size.w.min)) { child.resolved.size.w.min = child.layout.size.w.min + minWidthFromPadding; }
                if (!set(child.resolved.size.h.min)) { child.resolved.size.h.min = child.layout.size.h.min + minHeightFromPadding; }

                // If no set val, get from min
                if (!set(child.resolved.size.w.val)) { child.resolved.size.w.val = child.resolved.size.w.min; }
                if (!set(child.resolved.size.h.val)) { child.resolved.size.h.val = child.resolved.size.h.min; }
            }

            // Measure layout max prior to grow
            //--------------------------------------------------

            if (resolved.style.layout.direction == Axis::Vertical) {

                // Measure val
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.val = std::max(row.size.w.val, member->resolved.getOuter(Axis::Horizontal));
                        row.size.h.val += member->resolved.getOuter(Axis::Vertical);
                    }

                    layout.size.w.val += row.size.w.val;
                    layout.size.h.val = std::max(layout.size.h.val, row.size.h.val);
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.max = std::max(row.size.w.max, member->resolved.getMaxOuter(Axis::Horizontal));
                        row.size.h.max += member->resolved.getMaxOuter(Axis::Vertical);
                    }

                    layout.size.w.max += row.size.w.max;
                    layout.size.h.max = std::max(layout.size.h.max, row.size.h.max);
                }
            }

            else {

                // Measure val
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.val += member->resolved.getOuter(Axis::Horizontal);
                        row.size.h.val = std::max(row.size.h.val, member->resolved.getOuter(Axis::Vertical));
                    }

                    layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                    layout.size.h.val += row.size.h.val;
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {

                        if (!member->resolved.affectsParentSize) { continue; }

                        row.size.w.max += member->resolved.getMaxOuter(Axis::Horizontal);
                        row.size.h.max = std::max(row.size.h.max, member->resolved.getMaxOuter(Axis::Vertical));
                    }

                    layout.size.w.max = std::max(layout.size.w.max, row.size.w.max);
                    layout.size.h.max += row.size.h.max;
                }
            }

            // Clamp layout
            for (Row& row : layout.rows) {

                if (row.size.w.max < row.size.w.min) { row.size.w.max = row.size.w.min; }
                if (row.size.h.max < row.size.h.min) { row.size.h.max = row.size.h.min; }
            }

            if (layout.size.w.max < layout.size.w.min) { layout.size.w.max = layout.size.w.min; }
            if (layout.size.h.max < layout.size.h.min) { layout.size.h.max = layout.size.h.min; }

            // Grow layout and members
            //--------------------------------------------------

            if (resolved.style.layout.direction == Axis::Vertical) { this->growVerticalMode(); }
            else { this->growHorizontalMode(); }

            // Measure layout val after grow
            //--------------------------------------------------

            if (!layout.rows.empty()) {
   
                layout.size.w.val = -0.0f;
                layout.size.h.val = -0.0f;
            }

            if (resolved.style.layout.direction == Axis::Vertical) {

                // Measure val
                for (Row& row : layout.rows) {

                    row.size.w.val = -0.0f;
                    row.size.h.val = -0.0f;

                    for (Element* member : row.members) {
                        if (!member->resolved.affectsParentSize) { continue; }
                        row.size.w.val = std::max(row.size.w.val, member->resolved.getOuter(Axis::Horizontal));
                        row.size.h.val += member->resolved.getOuter(Axis::Vertical);
                    }

                    layout.size.w.val += row.size.w.val;
                    layout.size.h.val = std::max(layout.size.h.val, row.size.h.val);
                }
            }

            else {

                // Measure val
                for (Row& row : layout.rows) {

                    row.size.w.val = -0.0f;
                    row.size.h.val = -0.0f;

                    for (Element* member : row.members) {
                        if (!member->resolved.affectsParentSize) { continue; }
                        row.size.w.val += member->resolved.getOuter(Axis::Horizontal);
                        row.size.h.val = std::max(row.size.h.val, member->resolved.getOuter(Axis::Vertical));
                    }

                    layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                    layout.size.h.val += row.size.h.val;
                }
            }

            bool test = true;
        }
        
        void growHorizontalMode() {

            // Grow growable dimensions (horizontal)
            //--------------------------------------------------

            layout.size.w.max = std::min(layout.size.w.max, resolved.getInner(Axis::Horizontal));

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
                        float take = member->resolved.grow(share, Axis::Horizontal);
                        row.size.w.val += take;
                        availableWidth -= take;
                    }
                }
            }

            // Grow each row (vertical)
            //--------------------------------------------------

            // Consider moving back to "min" strategy to handle fitting
            layout.size.h.max = std::min(layout.size.h.max, resolved.getInner(Axis::Vertical));
            float availableHeight = layout.size.h.max - layout.size.h.val;

            // Loop until break conditions are met
            while (true) {

                int numGrowableH = layout.growableRows(Axis::Vertical);
                float shareH = availableHeight / float(numGrowableH);

                // When there's no more space or no more growable elements
                if (!numGrowableH || availableHeight < 0.01) {
                    break;
                }

                for (Row& row : layout.rows) {
                    float take = row.size.h.grow(shareH);
                    layout.size.h.val += take;
                    availableHeight -= take;
                }
            }

            // Grow each row member (vertical)
            //--------------------------------------------------

            for (Row& row : layout.rows) {
                for (Element* member : row.members) {
                    
                    Element& elem = *member;

                    float availableElemHeight = row.size.h.val - elem.resolved.getOuter(Axis::Vertical);

                    while (true) {
                        
                        int numGrowable = elem.resolved.canGrow(Axis::Vertical);
                        float share = availableElemHeight / float(numGrowable);

                        if (!numGrowable || availableElemHeight < 0.01) {
                            break;
                        }
                        
                        float take = elem.resolved.grow(share, Axis::Vertical);
                        availableElemHeight -= take;
                    }
                }
            }
        }

        void growVerticalMode() {

            // Grow each row (horizontal)
            //--------------------------------------------------

            // Consider moving back to "min" strategy to handle fitting
            layout.size.w.max = std::min(layout.size.w.max, resolved.getInner(Axis::Horizontal));
            float availableWidth = layout.size.w.max - layout.size.w.val;

            // Loop until break conditions are met
            while (true) {

                int numGrowableW = layout.growableRows(Axis::Horizontal);
                float shareW = availableWidth / float(numGrowableW);

                // When there's no more space or no more growable elements
                if (!numGrowableW || availableWidth < 0.01) {
                    break;
                }

                for (Row& row : layout.rows) {
                    float take = row.size.w.grow(shareW);
                    layout.size.w.val += take;
                    availableWidth -= take;
                }
            }

            // Grow each row member (horizontal)
            //--------------------------------------------------

            for (Row& row : layout.rows) {
                for (Element* member : row.members) {
                    
                    Element& elem = *member;

                    float availableElemWidth = row.size.w.val - elem.resolved.getOuter(Axis::Horizontal);

                    while (true) {
                        
                        int numGrowable = elem.resolved.canGrow(Axis::Horizontal);
                        float share = availableElemWidth / float(numGrowable);

                        if (!numGrowable || availableElemWidth < 0.01) {
                            break;
                        }
                        
                        float take = elem.resolved.grow(share, Axis::Horizontal);
                        availableElemWidth -= take;
                    }
                }
            }

            // Grow growable dimensions (horizontal)
            //--------------------------------------------------

            layout.size.h.max = std::min(layout.size.h.max, resolved.getInner(Axis::Vertical));

            for (Row& row : layout.rows) {

                row.size.h.max = std::min(row.size.h.max, layout.size.h.max);
                
                float availableHeight = row.size.h.max - row.size.h.val;

                // Loop until break conditions are met
                while (true) {

                    int numGrowable = row.canGrow(Axis::Vertical);
                    float share = availableHeight / float(numGrowable);

                    // When there's no more space or no more growable elements
                    if (!numGrowable || availableHeight < 0.01) {
                        break;
                    }

                    for (Element* member : row.members) {
                        float take = member->resolved.grow(share, Axis::Vertical);
                        row.size.h.val += take;
                        availableHeight -= take;
                    }
                }
            }
        }

        // Reusable center function
        float center(float parent, float child, Align align) {

            switch (align) {
                case (Align::Start): { return 0; break; }
                case (Align::End): { return parent - child; break; }
                case (Align::Center): { return (parent - child) / 2; break; }
                case (Align::Unset): { return 0; break; }
            }
        }

        // Resolve final positions (top down)
        void resolveRects() {

            if (resolved.hidden) { return; }

            // If top level
            if (parent == this) {
                rect = {
                    0, 0,
                    resolved.size.w.val, resolved.size.h.val
                };
            }

            // Requires children
            if (children.empty()) {
                return;
            }

            // Resolve dimensions
            //--------------------------------------------------

            // Resolve layout dimensions
            layout.rect.w = layout.size.w.val;
            layout.rect.h = layout.size.h.val;

            // Resolve row dimensions
            for (Row& row : layout.rows) {
                
                //row.size.clamp();
                row.rect.w = row.size.w.val;
                row.rect.h = row.size.h.val;

                // Resolve member dimensions
                for (Element* member : row.members) {
                    //member->resolved.size.clamp();
                    member->rect.w = member->resolved.size.w.val;
                    member->rect.h = member->resolved.size.h.val;
                }
            }

            // Resolve positions
            //--------------------------------------------------

            float layoutOffsetX = resolved.pad.l.val;
            float layoutOffsetY = resolved.pad.t.val;

            Style& rStyle = resolved.style;

            layoutOffsetX += center(resolved.getInner(Axis::Horizontal), layout.rect.w, rStyle.layout.horizontal);
            layoutOffsetY += center(resolved.getInner(Axis::Vertical), layout.rect.h, rStyle.layout.vertical);

            // Resolve layout position
            layout.rect.x = rect.x + layoutOffsetX;
            layout.rect.y = rect.y + layoutOffsetY;

            if (resolved.style.layout.direction == Axis::Vertical) {

                float runningX = 0;

                // Position rows
                for (Row& row : layout.rows) {

                    float rowOffsetY = center(layout.rect.h, row.rect.h, rStyle.layout.vertical);
                    
                    row.rect.x = layout.rect.x + runningX;
                    row.rect.y = layout.rect.y + rowOffsetY;

                    float runningY = 0;

                    Element* first = row.members.front();
                    Element* last = row.members.back();

                    for (Element* member : row.members) {

                        if (member->resolved.style.layout.position == Position::Absolute) {

                            member->rect.x = rect.x + member->resolved.mar.l.val;
                            member->rect.y = rect.y + member->resolved.mar.t.val;
                            
                            member->rect.x += member->resolved.pos.l.val;
                            member->rect.y += member->resolved.pos.t.val;

                            continue;
                        }

                        member->rect.x = row.rect.x + member->resolved.mar.l.val;
                        member->rect.y = row.rect.y + runningY + member->resolved.mar.t.val;

                        member->rect.x += member->resolved.pos.l.val;
                        member->rect.y += member->resolved.pos.t.val;
   
                        runningY += member->rect.h + member->resolved.mar.t.val + member->resolved.mar.b.val;
                    }

                    runningX += row.rect.w;
                }
            }

            else {

                float runningY = 0;

                // Position rows
                for (Row& row : layout.rows) {

                    float rowOffsetX = center(layout.rect.w, row.rect.w, rStyle.layout.horizontal);
                    
                    row.rect.x = layout.rect.x + rowOffsetX;
                    row.rect.y = layout.rect.y + runningY;

                    float runningX = 0;

                    Element* first = row.members.front();
                    Element* last = row.members.back();

                    for (Element* member : row.members) {

                        if (member->resolved.style.layout.position == Position::Absolute) {

                            member->rect.x = rect.x + member->resolved.mar.l.val;
                            member->rect.y = rect.y + member->resolved.mar.t.val;

                            member->rect.x += member->resolved.pos.l.val;
                            member->rect.y += member->resolved.pos.t.val;

                            continue;
                        }

                        member->rect.x = row.rect.x + runningX + member->resolved.mar.l.val;
                        member->rect.y = row.rect.y + member->resolved.mar.t.val;

                        member->rect.x += member->resolved.pos.l.val;
                        member->rect.y += member->resolved.pos.t.val;
                        
                        runningX += member->rect.w + member->resolved.mar.l.val + member->resolved.mar.r.val;
                    }

                    runningY += row.rect.h;
                }
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
        void onGainFocus(const std::function<void(Event&)>& listener) { this->listen(&Element::gainFocus, listener); }
        void onLoseFocus(const std::function<void(Event&)>& listener) { this->listen(&Element::loseFocus, listener); }
        void onMouseEnter(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseEnter, listener); }
        void onMouseLeave(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseLeave, listener); }
        void onMouseWheel(const std::function<void(Event&)>& listener) { this->listen(&Element::mouseWheel, listener); }
        void onKeyDown(const std::function<void(Event&)>& listener) { this->listen(&Element::keyDown, listener); }
        void onKeyUp(const std::function<void(Event&)>& listener) { this->listen(&Element::keyUp, listener); }
        void onTextInput(const std::function<void(Event&)>& listener) { this->listen(&Element::textInput, listener); }

        // Event propagation
        //--------------------------------------------------

        struct TargetFlags {
            bool hit = false;
            bool click = false;
            bool hover = false;
            bool press = false;
            bool focus = false;
            bool drag = false;
        };

        TargetFlags targetFlags;

        // Default behavior is to ask our rect if it contains a position
        virtual bool contains(Pos& pos) {
            return this->rect.contains(pos);
        }

        // When the element refreshes
        virtual void refresh(Event& e) {

            if (!this->dirty.draw) {

                this->dirty.draw = true;
                e.causedRefresh = true;
                shared->dirty.refresh.push_back(this);
            }
    
            // Propagate upwards
            if (parent && !parent->dirty.draw) {
                parent->refresh(e);
            }
        }

        // When the element gains focus
        virtual void gainFocus(Event& e) {

            if (!targetFlags.focus) {
                targetFlags.focus = true;
                if (resolved.hasFocusStyle) {
                    styles.dirty = true;
                }
            }

            // Tell event listeners
            tell(&Element::gainFocus, e);
            if (!e.propagate) { return; }

            // Propagate to children
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isFocusTarget = child.targetFlags.focus;

                if (containsEvent && !isFocusTarget) { child.gainFocus(e); }
                if (!e.propagate) { return; }
            }
        }

        // When the element loses focus
        virtual void loseFocus(Event& e) {

            if (targetFlags.focus) {
                targetFlags.focus = false;
                if (resolved.hasFocusStyle) { styles.dirty = true; }
            }

            tell(&Element::loseFocus, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isFocusTarget = child.targetFlags.focus;

                if (!containsEvent && isFocusTarget) { child.loseFocus(e); }
                if (!e.propagate) { return; }
            }
        }

        // When a mouse button is pressed
        virtual void mouseDown(Event& e) {

            // Mouse down event means we are a drag target
            if (!targetFlags.drag) {
                targetFlags.drag = true;
                if (resolved.hasDragStyle) { styles.dirty = true; }
            }

            // Tell event listeners
            tell(&Element::mouseDown, e);
            if (!e.propagate) { return; }

            // Propagate
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool isFocusTarget = child.targetFlags.focus;
                bool containsEvent = child.targetFlags.hit;

                if (containsEvent) { child.mouseDown(e); }
                if (containsEvent && !isFocusTarget) { child.gainFocus(e); }
                if (!containsEvent && isFocusTarget) { child.loseFocus(e); }
            
                if (!e.propagate) { return; }
            }
        }

        // When a mouse button is released
        virtual void mouseUp(Event& e) {

            // Mouseup means dragging must end
            if (targetFlags.drag) {
                targetFlags.drag = false;
                if (resolved.hasDragStyle) { styles.dirty = true; }
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

        // When the mouse moves on/over the element
        virtual void mouseMove(Event& e) {

            // If there is a cursor we need to set
            if (resolved.style.cursor != Cursor::Unset) {
                e.mouse.cursor = resolved.style.cursor;
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
                if (resolved.hasHoverStyle) {
                    styles.dirty = true;
                }
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

        // When a mouse leaves the element
        virtual void mouseLeave(Event& e) {

            if (targetFlags.hover) {
                targetFlags.hover = false;
                if (resolved.hasHoverStyle) { styles.dirty = true; }
            }

            tell(&Element::mouseLeave, e);
            if (!e.propagate) { return; }

            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                bool containsEvent = child.targetFlags.hit;
                bool isHoverTarget = child.targetFlags.hover;

                if (!containsEvent && isHoverTarget) { child.mouseLeave(e); }
                if (!e.propagate) { return; }
            }
        }

        // When the mouse drags in/on the element
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

        // When the mouse scrolls
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

        // When a key is pressed
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

        // When a key is released
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

        // When text is recieved
        virtual void textInput(Event& e) {

            tell(&Element::textInput, e);
            if (!e.propagate) { return; }

            // Propagate in reverse order
            for (Element* pChild : std::views::reverse(children)) {

                Element& child = *pChild;

                if (child.targetFlags.focus) { child.textInput(e); }
                if (!e.propagate) { return; }
            }
        }
    };
}