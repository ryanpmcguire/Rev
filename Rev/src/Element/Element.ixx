module;

#include <cmath>
#include <string>
#include <vector>
#include <ranges>
#include <functional>

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

    struct Element {

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
            bool draw = true;
        };

        // Shared betweeen elements
        Shared* shared = nullptr;

        // Self and parent
        Element* parent = nullptr;
        std::vector<Element*> children;
        std::string name = "Element";
        
        // Style
        StylePtr style;
        StyleList styles;
        
        // Computing
        Resolved resolved;
        Rect rect;

        // Tracking
        size_t draws = 0;
        size_t depth = 0;

        bool visible = true;
        bool scissor = false;

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

            resolved.style.inherit(parent->resolved.style);

            for (Element* child : children) {
                child->cascadeStyle();
            }
        }

        // Computing
        //--------------------------------------------------

        std::vector<Transition> transitions;

        virtual void animateStyle(Event& e) {

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

            // Calculate whether we have certain styles
            for (Style* style : styles.styles) {
                if (style->applies.hover) { resolved.hasHoverStyle = true; }
                if (style->applies.press) { resolved.hasPressStyle = true; }
                if (style->applies.drag) { resolved.hasDragStyle = true; }
                if (style->applies.focus) { resolved.hasFocusStyle = true; }
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

        virtual void computeChildren(Event& e) {}
        
        virtual void computeStyle(Event& e) {}

        // Compute attributes
        virtual void computePrimitives(Event& e) {}

        // Draw stencil (this must be seperate from draw logic)
        virtual void stencil(Event& e) {

        }

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
                    numCanGrow += member->resolved.canGrow(axis);
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

        // Step zero is to reset all data which will be modified
        void resetLayout() {

            // Reset res, rect, layout
            resolved.size = ResolvedSize();
            resolved.mar = ResolvedLrtb();
            resolved.pad = ResolvedLrtb();
            resolved.pos = ResolvedLrtb();

            rect = Rect();
            layout = Layout();
        }

        float getMinSize(Axis axis, Dist::Type type) {

            // Choose axis, prefer minimum if matching type
            Dist& minDist = (axis == Axis::Horizontal) ? resolved.style.size.minWidth : resolved.style.size.minHeight;
            Dist& nomDist = (axis == Axis::Horizontal) ? resolved.style.size.width : resolved.style.size.height;
            Dist& dist = (minDist.type == type) ? minDist : nomDist;

            // Return only if type matches
            if (dist.type == type) { return dist.val; }
            else { return -0.0f; }
        }

        float getMaxSize(Axis axis, Dist::Type type) {

            // Choose axis, prefer minimum if matching type
            Dist& maxDist = (axis == Axis::Horizontal) ? resolved.style.size.maxWidth : resolved.style.size.maxHeight;
            Dist& nomDist = (axis == Axis::Horizontal) ? resolved.style.size.width : resolved.style.size.height;
            Dist& dist = (maxDist.type == type) ? maxDist : nomDist;

            // Return only if type matches
            if (dist.type == type) { return dist.val; }
            else { return -0.0f; }
        }

        float getMinPadding(Axis axis, Dist::Type type) {

            float min = -0.0f;

            // Chose axis, prefer minimum if matching type
            Dist& minA = (axis == Axis::Horizontal) ? resolved.style.padding.minLeft : resolved.style.padding.minTop;
            Dist& nomA = (axis == Axis::Horizontal) ? resolved.style.padding.left : resolved.style.padding.top;
            Dist& a = (minA.type == type) ? minA : nomA;

            // Chose axis, prefer minimum if matching type
            Dist& minB = (axis == Axis::Horizontal) ? resolved.style.padding.minRight : resolved.style.padding.minBottom;
            Dist& nomB = (axis == Axis::Horizontal) ? resolved.style.padding.right : resolved.style.padding.bottom;
            Dist& b = (minB.type == type) ? minB : nomB;

            // Dimensions contribute only if matching type
            if (a.type == type) { min += a.val; }
            if (b.type == type) { min += b.val; }

            return min;
        }

        float getMinMargin(Axis axis, Dist::Type type) {

            float min = -0.0f;

            // Chose axis, prefer minimum if matching type
            Dist& minA = (axis == Axis::Horizontal) ? resolved.style.margin.minLeft : resolved.style.margin.minTop;
            Dist& nomA = (axis == Axis::Horizontal) ? resolved.style.margin.left : resolved.style.margin.top;
            Dist& a = (minA.type == type) ? minA : nomA;

            // Chose axis, prefer minimum if matching type
            Dist& minB = (axis == Axis::Horizontal) ? resolved.style.margin.minRight : resolved.style.margin.minBottom;
            Dist& nomB = (axis == Axis::Horizontal) ? resolved.style.margin.right : resolved.style.margin.bottom;
            Dist& b = (minB.type == type) ? minB : nomB;

            // Dimensions contribute only if matching type
            if (a.type == type) { min += a.val; }
            if (b.type == type) { min += b.val; }

            return min;
        }

        float minWidth, minHeight;

        float minMarginWidth, minMarginHeight;
        float minPaddingWidth, minPaddingHeight;

        float minOuterWidth, minOuterHeight;
        float minInnerWidth, minInnerHeight;

        void resolveMinimaNew() {

            // Reset all
            minWidth = minHeight = -0.0f;
            minMarginWidth = minMarginHeight = -0.0f;
            minPaddingWidth = minPaddingHeight = -0.0f;
            minOuterWidth = minOuterHeight = -0.0f;
            minInnerWidth = minInnerHeight = -0.0f;

            if (!visible) { return; }

            // Get from style
            //--------------------------------------------------

            minWidth = this->getMinSize(Axis::Horizontal, Dist::Type::Abs);
            minHeight = this->getMinSize(Axis::Vertical, Dist::Type::Abs);

            minPaddingWidth = this->getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            minPaddingHeight = this->getMinPadding(Axis::Vertical, Dist::Type::Abs);

            minMarginWidth = this->getMinMargin(Axis::Horizontal, Dist::Type::Abs);
            minMarginHeight = this->getMinMargin(Axis::Vertical, Dist::Type::Abs);

            // Infer from self (or children if necessary)
            //--------------------------------------------------

            if (set(minWidth)) {
                minInnerWidth = minWidth - minPaddingWidth;
                minOuterWidth = minWidth + minMarginWidth;
            }

            else {

                float maxOfMin = -0.0f;
                for (Element* c : children) { maxOfMin = std::max(maxOfMin, c->minOuterWidth); }
                
                minInnerWidth = maxOfMin;
                minOuterWidth = maxOfMin + minPaddingWidth + minMarginWidth;
            }

            if (set(minHeight)) {
                minInnerHeight = minHeight - minPaddingHeight;
                minOuterHeight = minHeight + minMarginHeight;
            }

            else {

                float maxOfMin = -0.0f;
                for (Element* c : children) { maxOfMin = std::max(maxOfMin, c->minOuterHeight); }

                minInnerHeight = maxOfMin;
                minOuterHeight = maxOfMin + minPaddingHeight + minMarginHeight;
            }
        }

        float maxWidth, maxHeight;
        float maxInnerWidth, maxInnerHeight;

        // Top down: resolve maximum feasible dimensions
        void resolveMaximaNew() {

            // MAXIMUM inner size
            maxInnerWidth = -0.0f;
            maxInnerHeight = -0.0f;

            if (!visible) { return; }
        
            // Get from style
            //--------------------------------------------------
            
            float maxWidth = this->getMaxSize(Axis::Horizontal, Dist::Type::Abs);
            float maxHeight = this->getMaxSize(Axis::Vertical, Dist::Type::Abs);

            // Infer from set values
            //--------------------------------------------------

            if (set(maxWidth)) { maxInnerWidth = maxWidth - minPaddingWidth; }
            else { maxInnerWidth = parent->maxInnerWidth - minMarginWidth - minPaddingWidth; }

            if (set(maxHeight)) { maxInnerHeight = maxHeight - minPaddingHeight; }
            else { maxInnerHeight = parent->maxInnerHeight - minMarginHeight - minPaddingHeight; }

            // Subtract subling outer heights if no set height
            if (!set(maxHeight)) {
                for (Element* s : parent->children) {
                    if (s == this) { continue; }
                    //maxInnerHeight -= s->minOuterHeight;
                }
            }

            // Ensure minimum dominates (in certain circumstances)
            if (maxInnerWidth < minInnerWidth) { maxInnerWidth = minInnerWidth; }
            if (maxInnerHeight < minInnerHeight) { maxInnerHeight = minInnerHeight; }
        }

        void resolveLayoutNew() {

            if (!visible) { return; }

            // Wrap children
            //--------------------------------------------------

            Row row = Row();
            float runningRelSize = 0.0f;

            for (Element* child : children) {
                
                bool horizontal = (resolved.style.alignment.direction != Axis::Vertical);
                Axis axis = horizontal ? Axis::Horizontal : Axis::Vertical;

                // Get minimum abs/rel sizes
                //--------------------------------------------------

                float& maxAbsInnerSize = horizontal ? maxInnerWidth : maxInnerHeight;
                float& minAbsLayoutSize = horizontal ? child->layout.size.w.min : child->layout.size.h.min;

                // These absolute minima were already calculated before
                float& minAbsSize = horizontal ? child->minWidth : child->minHeight;
                float& minAbsMargin = horizontal ? child->minMarginWidth : child->minMarginHeight;
                float& minAbsPadding = horizontal ? child->minPaddingWidth : child->minPaddingHeight;

                // We directly get the proportional values
                float minRelSize = child->getMinSize(axis, Dist::Type::Rel);
                float minRelMargin = child->getMinMargin(axis, Dist::Type::Rel);
                float minRelPadding = child->getMinPadding(axis, Dist::Type::Rel); 

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

            // Set parent layout/row for each child
            for (Row& row : layout.rows) {
                for (Element* member : row.members) {
                    member->parentLayout = &layout;
                    member->parentRow = &row;
                }
            }

            // Measure layout val/min
            //--------------------------------------------------

            if (resolved.style.alignment.direction == Axis::Vertical) {
            
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
    
                        row.size.w.min = std::max(row.size.w.min, member->minOuterWidth);
                        row.size.h.min += member->minOuterHeight;
                    }

                    layout.size.w.min += row.size.w.min;
                    layout.size.h.min = std::max(layout.size.h.min, row.size.h.min);
                }
            }

            else {

                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
    
                        row.size.w.min += member->minOuterWidth;
                        row.size.h.min = std::max(row.size.h.min, member->minOuterHeight);
                    }
    
                    layout.size.w.min = std::max(layout.size.w.min, row.size.w.min);
                    layout.size.h.min += row.size.h.min;
                }
            }

            // Adjust own minimum outer size to accomodate layout
            //--------------------------------------------------

            if (set(minWidth)) { minOuterWidth = minWidth + minMarginWidth; }
            else { minOuterWidth = layout.size.w.min + minPaddingWidth + minMarginWidth; }

            if (set(minHeight)) { minOuterHeight = minHeight + minMarginHeight; }
            else { minOuterHeight = layout.size.h.min + minPaddingHeight + minMarginHeight; }

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

                    if (elem.resolved.canGrow(Axis::Horizontal)) {
                        resolved.size.w.growable = true;
                    }

                    if (elem.resolved.canGrow(Axis::Vertical)) {
                        resolved.size.w.growable = true;
                        row.size.h.growable = true;
                        layout.size.h.growable = true;
                    }
                }
            }
        }

        float innerWidth;
        float innerHeight;

        // Top down: Resolve flex and grow dimensions
        void resolveDimsNew() {

            innerWidth = 0;
            innerHeight = 0;

            if (!visible) { return; }

            Resolved& res = resolved;
            
            if (parent == this) {
                res.size.w.val = res.size.w.min = res.size.w.max = resolved.style.size.width.val;
                res.size.h.val = res.size.h.min = res.size.h.max = resolved.style.size.height.val;
            }

            innerWidth = res.size.w.val;
            innerHeight = res.size.h.val;

            // Resolve own padding (needed for fit)
            //--------------------------------------------------
            
            res.pad.l.val = res.pad.l.min = res.pad.l.max = resolved.style.padding.left.val;
            res.pad.r.val = res.pad.r.min = res.pad.r.max = resolved.style.padding.right.val;
            res.pad.t.val = res.pad.t.min = res.pad.t.max = resolved.style.padding.top.val;
            res.pad.b.val = res.pad.b.min = res.pad.b.max = resolved.style.padding.bottom.val;

            innerWidth -= res.pad.l.val + res.pad.r.val;
            innerHeight -= res.pad.t.val + res.pad.b.val;

            bool testa = true;

            // Resolve val/max of children prior to grow
            //--------------------------------------------------

            for (Element* pChild : children) {

                Element& child = *pChild;
                Size& cSize = child.resolved.style.size;
                LrtbStyle& cMargin = child.resolved.style.margin;
                LrtbStyle& cPadding = child.resolved.style.padding;
                Dist& cWidth = cSize.width;
                Dist& cHeight = cSize.height;

                // Resolve nominal
                if (cSize.width) { child.resolved.size.w.val = child.resolved.size.w.min = child.resolved.size.w.max = cSize.width.resolve(innerWidth); }
                if (cSize.height) { child.resolved.size.h.val = child.resolved.size.h.min = child.resolved.size.h.max = cSize.height.resolve(innerHeight); }

                // Resolve min
                if (cSize.minWidth) { child.resolved.size.w.min = cSize.minWidth.resolve(innerWidth); }
                if (cSize.minHeight) { child.resolved.size.h.min = cSize.minHeight.resolve(innerHeight); }

                // Resolve max
                if (cSize.maxWidth) { child.resolved.size.w.max = cSize.maxWidth.resolve(innerWidth); }
                if (cSize.maxHeight) { child.resolved.size.h.max = cSize.maxHeight.resolve(innerHeight); }

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

                float minWidthFromPadding = child.resolved.pad.l.min + child.resolved.pad.r.min;
                float minHeightFromPadding = child.resolved.pad.t.min + child.resolved.pad.b.min;

                if (!set(child.resolved.size.w.min)) { child.resolved.size.w.min = child.layout.size.w.min + minWidthFromPadding; }
                if (!set(child.resolved.size.h.min)) { child.resolved.size.h.min = child.layout.size.h.min + minHeightFromPadding; }

                // If no set val, get from min
                if (!set(child.resolved.size.w.val)) { child.resolved.size.w.val = child.resolved.size.w.min; }
                if (!set(child.resolved.size.h.val)) { child.resolved.size.h.val = child.resolved.size.h.min; }

                bool test = true;
            }

            // Measure layout max prior to grow
            //--------------------------------------------------

            bool testb = true;

            if (resolved.style.alignment.direction == Axis::Vertical) {

                // Measure val
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
                        row.size.w.val = std::max(row.size.w.val, member->resolved.getOuter(Axis::Horizontal));
                        row.size.h.val += member->resolved.getOuter(Axis::Vertical);
                    }

                    layout.size.w.val += row.size.w.val;
                    layout.size.h.val = std::max(layout.size.h.val, row.size.h.val);
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
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
                        row.size.w.val += member->resolved.getOuter(Axis::Horizontal);
                        row.size.h.val = std::max(row.size.h.val, member->resolved.getOuter(Axis::Vertical));
                    }

                    layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                    layout.size.h.val += row.size.h.val;
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
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

            if (resolved.style.alignment.direction == Axis::Vertical) { this->growVerticalMode(); }
            else { this->growHorizontalMode(); }

            // Measure layout val after grow
            //--------------------------------------------------

            layout.size.w.val = -0.0f;
            layout.size.h.val = -0.0f;

            if (resolved.style.alignment.direction == Axis::Vertical) {

                // Measure val
                for (Row& row : layout.rows) {

                    row.size.w.val = -0.0f;
                    row.size.h.val = -0.0f;

                    for (Element* member : row.members) {
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

            // Grow each row (vertical)
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

            // Grow each row member (vertical)
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

            if (!visible) { return; }

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

            layoutOffsetX += center(resolved.getInner(Axis::Horizontal), layout.rect.w, rStyle.alignment.horizontal);
            layoutOffsetY += center(resolved.getInner(Axis::Vertical), layout.rect.h, rStyle.alignment.vertical);

            // Resolve layout position
            layout.rect.x = rect.x + layoutOffsetX;
            layout.rect.y = rect.y + layoutOffsetY;

            if (resolved.style.alignment.direction == Axis::Vertical) {

                float runningX = 0;

                // Position rows
                for (Row& row : layout.rows) {

                    float rowOffsetY = center(layout.rect.h, row.rect.h, rStyle.alignment.vertical);
                    
                    row.rect.x = layout.rect.x + runningX;
                    row.rect.y = layout.rect.y + rowOffsetY;

                    float runningY = 0;

                    Element* first = row.members.front();
                    Element* last = row.members.back();

                    for (Element* member : row.members) {

                        member->rect.x = member->resolved.mar.l.val + row.rect.x;
                        member->rect.y = member->resolved.mar.t.val + row.rect.y + runningY;

                        runningY += member->rect.h + member->resolved.mar.t.val + member->resolved.mar.b.val;

                        // Apply relative positions
                        //--------------------------------------------------

                        if (member->resolved.pos.l.val != -0.0f) { member->rect.x += member->resolved.pos.l.val; }
                        if (member->resolved.pos.r.val != -0.0f) { member->rect.x += member->resolved.pos.r.val; }
                        if (member->resolved.pos.t.val != -0.0f) { member->rect.y += member->resolved.pos.t.val; }
                        if (member->resolved.pos.b.val != -0.0f) { member->rect.y += member->resolved.pos.b.val; }
                    }

                    runningX += row.rect.w;
                }
            }

            else {

                float runningY = 0;

                // Position rows
                for (Row& row : layout.rows) {

                    float rowOffsetX = center(layout.rect.w, row.rect.w, rStyle.alignment.horizontal);
                    
                    row.rect.x = layout.rect.x + rowOffsetX;
                    row.rect.y = layout.rect.y + runningY;

                    float runningX = 0;

                    Element* first = row.members.front();
                    Element* last = row.members.back();

                    for (Element* member : row.members) {

                        member->rect.x = member->resolved.mar.l.val + row.rect.x + runningX;
                        member->rect.y = member->resolved.mar.t.val + row.rect.y;

                        runningX += member->rect.w + member->resolved.mar.l.val + member->resolved.mar.r.val;

                        // Apply relative positions
                        //--------------------------------------------------

                        if (member->resolved.pos.l.val != -0.0f) { member->rect.x += member->resolved.pos.l.val; }
                        if (member->resolved.pos.r.val != -0.0f) { member->rect.x += member->resolved.pos.r.val; }
                        if (member->resolved.pos.t.val != -0.0f) { member->rect.y += member->resolved.pos.t.val; }
                        if (member->resolved.pos.b.val != -0.0f) { member->rect.y += member->resolved.pos.b.val; }
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
            bool press = false;
            bool focus = false;
            bool drag = false;
        };

        TargetFlags targetFlags;

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

                // If child contains event, we propagate
                if (child.targetFlags.hit) { child.mouseDown(e); }
                if (!e.propagate) { return; }
            }
        }

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

        // When the mouse moves on/over an element
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

        // When a mouse leaves an element
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