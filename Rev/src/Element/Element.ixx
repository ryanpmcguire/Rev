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

import Rev.Element.Style;
import Rev.Element.Computed;
import Rev.Element.Event;
import Rev.Element.Resolved;

export namespace Rev::Element {

    struct Element {

        struct Shared {

            std::vector<Element*> dirtyElements;
            std::vector<Element*> stencilStack;
            
            Graphics::Canvas* canvas = nullptr;
            Event* event = nullptr;
        };

        // Shared betweeen elements
        Shared* shared = nullptr;

        // Self and parent
        Element* parent = nullptr;
        std::vector<Element*> children;
        std::string name = "Element";

        // Style
        StylePtr style;
        std::vector<Style*> styles;
        
        // Computing
        bool dirty = false;
        Computed computed;
        Rect rect;

        // Tracking
        size_t draws = 0;
        size_t depth = 0;
        size_t scissor = false;

        // Create
        Element(Element* parent = nullptr, StyleList styles = {}, std::string name = "") {

            this->inherit(parent);
  
            this->styles = styles;
            this->name = name;
        }
        
        // Destroy
        virtual ~Element() {

            //dbg("[Element] destroying");

            // Remove all children
            for (Element* child : children) { if (child) { delete child; } }
            children.clear();
        }

        // Cast as pointer to canvas
        explicit operator Graphics::Canvas*() {
            return shared ? shared->canvas : nullptr;
        }

        // Inheritance
        //--------------------------------------------------

        void inherit(Element* parent) {
            
            if (parent) {
                
                this->parent = parent;

                parent->children.push_back(this);
                shared = parent->shared;
                this->refresh(*shared->event);
            }
        }

        // Computing
        //--------------------------------------------------

        std::vector<Transition> transitions;

        // Comptue style
        virtual void computeStyle(Event& e) {

            computed.hasHoverStyle = false;
            computed.hasPressStyle = false;
            computed.hasDragStyle = false;
            computed.hasFocusStyle = false;

            // Calculate whether we have certain styles
            for (Style* style : styles) {
                if (style->applies.hover) { computed.hasHoverStyle = true; }
                if (style->applies.press) { computed.hasPressStyle = true; }
                if (style->applies.drag) { computed.hasDragStyle = true; }
                if (style->applies.focus) { computed.hasFocusStyle = true; }
            }

            // Compile / apply styles
            //--------------------------------------------------

            Style old = computed.style;

            computed.style = Style();

            Applies flags = {
                .hover = targetFlags.hover,
                .press = targetFlags.press,
                .drag = targetFlags.drag,
                .focus = targetFlags.focus,
            };

            // Apply other styles, then own style
            computed.style.apply(styles, flags);
            computed.style.apply(style, flags);

            // If this is our first draw, we do not animate
            if (draws == 0) {
                return;
            }

            //return;

            // Create transitions if needed
            //--------------------------------------------------
            
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
        
        // Compute attributes
        virtual void computePrimitives(Event& e) {}

        // Draw stencil (this must be seperate from draw logic)
        virtual void stencil(Event& e) {

        }

        // Draw color
        virtual void draw(Event& e) {

            // Draw = no longer dirty
            this->dirty = false;
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

        // Step zero is to reset all data which will be modified
        void resetLayout() {

            // Reset res, rect, layout
            res = Resolved();
            rect = Rect();
            layout = Layout();
        }

        // New layout
        //--------------------------------------------------

        float minOuterWidth;
        float minOuterHeight;

        float minInnerWidth;
        float minInnerHeight;

        void resolveMinimaNew() {

            minInnerWidth = -0.0f;
            minInnerWidth = -0.0f;

            float maxOfMinChildOuterWidths = -0.0f;
            float maxOfMinChildOuterHeights = -0.0f;

            for (Element* child : children) {

                maxOfMinChildOuterWidths = std::max(maxOfMinChildOuterWidths, child->minOuterWidth);
                maxOfMinChildOuterHeights = std::max(maxOfMinChildOuterHeights, child->minOuterHeight);
            }

            // Calculate true theoretical minimum inner width
            //--------------------------------------------------

            Size& size = computed.style.size;
            LrtbStyle& padding = computed.style.padding;
            LrtbStyle& margin = computed.style.margin;

            float minAbsWidth = -0.0f;
            if (size.width.type == Dist::Type::Abs) { minAbsWidth = size.width.val; }
            if (size.minWidth.type == Dist::Type::Abs) { minAbsWidth = size.minWidth.val; }
            
            float minAbsHeight = -0.0f;
            if (size.height.type == Dist::Type::Abs) { minAbsHeight = size.height.val; }
            if (size.minHeight.type == Dist::Type::Abs) { minAbsHeight = size.minHeight.val; }

            float minAbsPaddingWidth = -0.0f;
            if (padding.left.type == Dist::Type::Abs) { minAbsPaddingWidth += padding.left.val; }
            if (padding.right.type == Dist::Type::Abs) { minAbsPaddingWidth += padding.right.val; }

            float minAbsPaddingHeight = -0.0f;
            if (padding.top.type == Dist::Type::Abs) { minAbsPaddingHeight += padding.top.val; }
            if (padding.bottom.type == Dist::Type::Abs) { minAbsPaddingHeight += padding.bottom.val; }

            float minAbsMarginWidth = -0.0f;
            if (margin.left.type == Dist::Type::Abs) { minAbsMarginWidth += margin.left.val; }
            if (margin.right.type == Dist::Type::Abs) { minAbsMarginWidth += margin.right.val; }

            float minAbsMarginHeight = -0.0f;
            if (margin.top.type == Dist::Type::Abs) { minAbsMarginHeight += margin.top.val; }
            if (margin.bottom.type == Dist::Type::Abs) { minAbsMarginHeight += margin.bottom.val; }

            // Calc min inner
            minInnerWidth = maxOfMinChildOuterWidths;
            minInnerHeight = maxOfMinChildOuterHeights;

            if (set(minAbsWidth)) { minInnerWidth = minAbsWidth - minAbsPaddingWidth; }
            if (set(minAbsHeight)) { minInnerHeight = minAbsHeight - minAbsPaddingHeight; }

            // Calc min outer
            minOuterWidth = maxOfMinChildOuterWidths + minAbsPaddingWidth;
            minOuterHeight = maxOfMinChildOuterHeights + minAbsPaddingHeight;

            if (set(minAbsWidth)) { minOuterWidth = minAbsWidth; }
            if (set(minAbsHeight)) { minOuterHeight = minAbsHeight; }

            minOuterWidth += minAbsMarginWidth;
            minOuterHeight += minAbsMarginHeight;
        }

        float maxInnerWidth;
        float maxInnerHeight;

        // Top down: resolve maximum feasible dimensions
        void resolveMaximaNew() {

            // MAXIMUM inner size
            maxInnerWidth = -0.0f;
            maxInnerHeight = -0.0f;

            // Get from style
            //--------------------------------------------------

            Size& size = computed.style.size;
            LrtbStyle& padding = computed.style.padding;
            LrtbStyle& margin = computed.style.margin;
        
            // Get maximum general dimensions
            //--------------------------------------------------
            
            float maxWidth = -0.0f;
            float maxHeight = -0.0f;

            // Get max width from max (or fall back to value)
            if (size.maxWidth.type == Dist::Type::Abs) { maxWidth = size.maxWidth.val; }
            else if (size.width.type == Dist::Type::Abs) { maxWidth = size.width.val; }

            // Set max height from max (or fall back to value)
            if (size.maxHeight.type == Dist::Type::Abs) { maxHeight = size.maxHeight.val; }
            else if (size.height.type == Dist::Type::Abs) { maxHeight = size.height.val; }

            // Get MINIMUM padding (padding *subtracts* from inner size)
            //--------------------------------------------------

            float minPaddingWidth = -0.0f;
            float minPaddingHeight = -0.0f;

            // Set min padding from minima (or fall back to value)
            if (padding.maxLeft.type == Dist::Type::Abs) { minPaddingWidth += padding.maxLeft.val; }
            else if (padding.left.type == Dist::Type::Abs) { minPaddingWidth += padding.left.val; }

            if (padding.maxRight.type == Dist::Type::Abs) { minPaddingWidth += padding.maxRight.val; }
            else if (padding.right.type == Dist::Type::Abs) { minPaddingWidth += padding.right.val; }

            if (padding.maxTop.type == Dist::Type::Abs) { minPaddingHeight += padding.maxTop.val; }
            else if (padding.top.type == Dist::Type::Abs) { minPaddingHeight += padding.top.val; }

            if (padding.maxBottom.type == Dist::Type::Abs) { minPaddingHeight += padding.maxBottom.val; }
            else if (padding.bottom.type == Dist::Type::Abs) { minPaddingHeight += padding.bottom.val; }
            
            float minMarginWidth = margin.left.val + margin.right.val;
            float minMarginHeight = margin.top.val + margin.bottom.val;

            // Infer from set values
            //--------------------------------------------------

            // Default is draw from parent
            maxInnerWidth = parent->maxInnerWidth - minMarginWidth;
            maxInnerHeight = parent->maxInnerHeight - minMarginHeight;

            // If we have an actual size, we set that as the new maximum
            if (set(maxWidth)) { maxInnerWidth = maxWidth; }
            if (set(maxHeight)) { maxInnerHeight = maxHeight; }

            // Padding always subtracts from inner width
            maxInnerWidth -= minPaddingWidth;
            maxInnerHeight -= minPaddingHeight;

            // Ensure minimum dominates (in certain circumstances)
            if (maxInnerWidth < minInnerWidth) { maxInnerWidth = minInnerWidth; }
            if (maxInnerHeight < minInnerHeight) { maxInnerHeight = minInnerHeight; }
        }

        void resolveLayoutNew() {

            //if (children.empty()) { return; }

            // Wrap children
            //--------------------------------------------------

            Row row = Row();
            float runningRelSpace = 0.0f;

            for (Element* child : children) {

                Size& size = child->computed.style.size;
                LrtbStyle& padding = child->computed.style.padding;
                LrtbStyle& margin = child->computed.style.margin;

                // Get from style
                //--------------------------------------------------

                float minAbsWidth = -0.0f;
                if (size.width.type == Dist::Type::Abs) { minAbsWidth = size.width.val; }
                if (size.minWidth.type == Dist::Type::Abs) { minAbsWidth = size.minWidth.val; }
                
                float minAbsHeight = -0.0f;
                if (size.height.type == Dist::Type::Abs) { minAbsHeight = size.height.val; }
                if (size.minHeight.type == Dist::Type::Abs) { minAbsHeight = size.minHeight.val; }

                float minRelWidth = -0.0f;
                if (size.width.type == Dist::Type::Rel) { minRelWidth = size.width.val; }
                if (size.minWidth.type == Dist::Type::Rel) { minRelWidth = size.minWidth.val; }

                float minRelHeight = -0.0f;
                if (size.height.type == Dist::Type::Rel) { minRelHeight = size.height.val; }
                if (size.minHeight.type == Dist::Type::Rel) { minRelHeight = size.minHeight.val; }

                float minAbsPaddingWidth = -0.0f;
                if (padding.left.type == Dist::Type::Abs) { minAbsPaddingWidth += padding.left.val; }
                if (padding.right.type == Dist::Type::Abs) { minAbsPaddingWidth += padding.right.val; }

                float minAbsPaddingHeight = -0.0f;
                if (padding.top.type == Dist::Type::Abs) { minAbsPaddingHeight += padding.top.val; }
                if (padding.bottom.type == Dist::Type::Abs) { minAbsPaddingHeight += padding.bottom.val; }

                float minRelPaddingWidth = -0.0f;
                if (padding.left.type == Dist::Type::Rel) { minRelPaddingWidth += padding.left.val; }
                if (padding.right.type == Dist::Type::Rel) { minRelPaddingWidth += padding.right.val ; }

                float minRelPaddingHeight = -0.0f;
                if (padding.top.type == Dist::Type::Rel) { minRelPaddingHeight += padding.top.val; }
                if (padding.bottom.type == Dist::Type::Rel) { minRelPaddingHeight += padding.bottom.val ; }

                float minAbsMarginWidth = -0.0f;
                if (margin.right.type == Dist::Type::Abs) { minAbsMarginWidth += margin.right.val; }
                if (margin.left.type == Dist::Type::Abs) { minAbsMarginWidth += margin.left.val; }

                float minAbsMarginHeight = -0.0f;
                if (margin.top.type == Dist::Type::Abs) { minAbsMarginHeight += margin.top.val; }
                if (margin.bottom.type == Dist::Type::Abs) { minAbsMarginHeight += margin.bottom.val; }

                float minRelMarginWidth = -0.0f;
                if (margin.right.type == Dist::Type::Rel) { minRelMarginWidth += margin.right.val; }
                if (margin.left.type == Dist::Type::Rel) { minRelMarginWidth += margin.left.val; }

                float minRelMarginHeight = -0.0f;
                if (margin.top.type == Dist::Type::Rel) { minRelMarginHeight += margin.top.val; }
                if (margin.bottom.type == Dist::Type::Rel) { minRelMarginHeight += margin.bottom.val; }

                // Infer from set values
                //--------------------------------------------------

                // Set child's min outer width as sum of absolute widths
                float minAbsOuterWidth = -0.0f;
                float minAbsLayoutWidth = child->layout.size.w.min;
                minAbsOuterWidth = minAbsLayoutWidth + minAbsPaddingWidth;
                if (set(minAbsWidth)) { minAbsOuterWidth = minAbsWidth; }
                minAbsOuterWidth += minAbsMarginWidth;
                child->minOuterWidth = minAbsOuterWidth;

                // Set child's min outer height as sum of absolute heights
                float minAbsOuterHeight = -0.0f;
                float minAbsLayoutHeight = child->layout.size.h.min;
                minAbsOuterHeight = minAbsLayoutHeight + minAbsPaddingHeight;
                if (set(minAbsHeight)) { minAbsOuterHeight = minAbsHeight; }
                minAbsOuterHeight += minAbsMarginHeight;
                child->minOuterHeight = minAbsOuterHeight;

                // Calculate added relative width and and wrap
                //--------------------------------------------------
                
                // Adjust absolute width to be relative to max inner width
                if (set(minAbsWidth)) { minRelWidth += minAbsWidth / maxInnerWidth; }
                else { minRelWidth += (child->layout.size.w.min + minAbsPaddingWidth) / maxInnerWidth; }
                if (set(minAbsMarginWidth)) { minRelMarginWidth += minAbsMarginWidth / maxInnerWidth; }

                // Adjust absolute height to be relative to max inner width
                if (set(minAbsHeight)) { minRelHeight += minAbsHeight / maxInnerHeight; }
                else { minRelHeight += (child->layout.size.h.min + minAbsPaddingHeight) / maxInnerHeight; }
                if (set(minAbsMarginHeight)) { minRelMarginHeight += minAbsMarginHeight / maxInnerHeight; }

                float minRelOuterWidth = -0.0f;
                float minRelLayoutWidth = child->layout.size.w.min / maxInnerWidth;

                // Default is to accomodate children (and pad)
                minRelOuterWidth = minRelLayoutWidth + minRelPaddingWidth;
                if (set(minRelWidth)) { minRelOuterWidth = minRelWidth; }
                minRelOuterWidth += minRelMarginWidth;
                float additionalRelWidth = minRelOuterWidth;

                float minRelOuterHeight = -0.0f;
                float minRelLayoutHeight = child->layout.size.w.min / maxInnerHeight;

                // Default is to accomodate children (and pad)
                minRelOuterHeight = minRelLayoutHeight + minRelPaddingHeight;
                if (set(minRelHeight)) { minRelOuterHeight = minRelHeight; }
                minRelOuterHeight += minRelMarginHeight;
                float additionalRelHeight = minRelOuterHeight;

                float additionalRelSpace = -0.0f;
                if (computed.style.alignment.direction == Axis::Vertical) { additionalRelSpace = additionalRelHeight; }
                else { additionalRelSpace = additionalRelWidth; }

                // Should we create a new row, or are we add more?
                // (In other words, have we exceeded the maximum inner width)
                if (!row.members.empty() && (runningRelSpace + additionalRelSpace > 1.0)) {
                    
                    // Push back, create new row
                    layout.rows.push_back(row);
                    row = Row();
                    runningRelSpace = 0;
                }

                runningRelSpace += additionalRelSpace;

                row.members.push_back(child);
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

            if (computed.style.alignment.direction == Axis::Vertical) {
            
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

            Size& size = computed.style.size;
            LrtbStyle& padding = computed.style.padding;
            LrtbStyle& margin = computed.style.margin;

            // Get from style
            //--------------------------------------------------

            float minAbsWidth = -0.0f;
            if (size.width.type == Dist::Type::Abs) { minAbsWidth = size.width.val; }
            if (size.minWidth.type == Dist::Type::Abs) { minAbsWidth = size.minWidth.val; }
            
            float minAbsHeight = -0.0f;
            if (size.height.type == Dist::Type::Abs) { minAbsHeight = size.height.val; }
            if (size.minHeight.type == Dist::Type::Abs) { minAbsHeight = size.minHeight.val; }

            float minAbsPaddingWidth = -0.0f;
            if (padding.left.type == Dist::Type::Abs) { minAbsPaddingWidth += padding.left.val; }
            if (padding.right.type == Dist::Type::Abs) { minAbsPaddingWidth += padding.right.val; }

            float minAbsPaddingHeight = -0.0f;
            if (padding.top.type == Dist::Type::Abs) { minAbsPaddingHeight += padding.top.val; }
            if (padding.bottom.type == Dist::Type::Abs) { minAbsPaddingHeight += padding.bottom.val; }

            float minAbsMarginWidth = -0.0f;
            if (margin.right.type == Dist::Type::Abs) { minAbsMarginWidth += margin.right.val; }
            if (margin.left.type == Dist::Type::Abs) { minAbsMarginWidth += margin.left.val; }

            float minAbsMarginHeight = -0.0f;
            if (margin.top.type == Dist::Type::Abs) { minAbsMarginHeight += margin.top.val; }
            if (margin.bottom.type == Dist::Type::Abs) { minAbsMarginHeight += margin.bottom.val; }

            float minAbsOuterWidth = -0.0f;
            float minAbsOuterHeight = -0.0f;

            minOuterWidth = layout.size.w.min + minAbsPaddingWidth;
            minOuterHeight = layout.size.h.min + minAbsPaddingHeight;

            if (set(minAbsWidth)) { minOuterWidth = minAbsWidth; }
            if (set(minAbsHeight)) { minOuterHeight = minAbsHeight; }

            minOuterWidth += minAbsMarginWidth;
            minOuterHeight += minAbsMarginHeight;

            // Restrict min outer dims to max size, if set
            //if (set(res.size.w.max) && minOuterWidth > res.size.w.max) { minOuterWidth = res.size.w.max; }
            //if (set(res.size.h.max) && minOuterHeight > res.size.h.max) { minOuterHeight = res.size.h.max;}

            // Modify own size to accomodate layout
            //--------------------------------------------------

            float layoutPlusPaddingWidth = layout.size.w.min + computed.style.padding.left.val + computed.style.padding.right.val;
            float layoutPlusPaddingHeight = layout.size.h.min + computed.style.padding.top.val + computed.style.padding.bottom.val;

            if (!set(res.size.w.min) && res.size.w.min < layoutPlusPaddingWidth) { res.size.w.min = layoutPlusPaddingWidth; }
            if (!set(res.size.h.min) && res.size.h.min < layoutPlusPaddingHeight) { res.size.h.min = layoutPlusPaddingHeight; }

            bool test = true;
        }

        // Bottom up: promote growability based on layout
        void promoteDimsNew() {

            // Tell dimensions if they can grow
            if (computed.style.size.width.type == Dist::Type::Grow) { res.size.w.growable = true; }
            if (computed.style.size.height.type == Dist::Type::Grow) { res.size.h.growable = true; }

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
        }

        float innerWidth;
        float innerHeight;

        // Top down: Resolve flex and grow dimensions
        void resolveDimsNew() {

            innerWidth = 0;
            innerHeight = 0;
            
            if (parent == this) {
                res.size.w.val = res.size.w.min = res.size.w.max = computed.style.size.width.val;
                res.size.h.val = res.size.h.min = res.size.h.max = computed.style.size.height.val;
            }

            innerWidth = res.size.w.val;
            innerHeight = res.size.h.val;

            // Resolve own padding (needed for fit)
            //--------------------------------------------------
            
            res.pad.l.val = res.pad.l.min = res.pad.l.max = computed.style.padding.left.val;
            res.pad.r.val = res.pad.r.min = res.pad.r.max = computed.style.padding.right.val;
            res.pad.t.val = res.pad.t.min = res.pad.t.max = computed.style.padding.top.val;
            res.pad.b.val = res.pad.b.min = res.pad.b.max = computed.style.padding.bottom.val;

            innerWidth -= res.pad.l.val + res.pad.r.val;
            innerHeight -= res.pad.t.val + res.pad.b.val;

            bool testa = true;

            // Resolve val/max of children prior to grow
            //--------------------------------------------------

            for (Element* pChild : children) {

                Element& child = *pChild;
                Size& cSize = child.computed.style.size;
                LrtbStyle& cMargin = child.computed.style.margin;
                LrtbStyle& cPadding = child.computed.style.padding;
                Dist& cWidth = cSize.width;
                Dist& cHeight = cSize.height;

                // Resolve nominal
                if (cSize.width) { child.res.size.w.val = child.res.size.w.min = child.res.size.w.max = cSize.width.resolve(innerWidth); }
                if (cSize.height) { child.res.size.h.val = child.res.size.h.min = child.res.size.h.max = cSize.height.resolve(innerHeight); }

                // Resolve min
                if (cSize.minWidth) { child.res.size.w.min = cSize.minWidth.resolve(innerWidth); }
                if (cSize.minHeight) { child.res.size.h.min = cSize.minHeight.resolve(innerHeight); }

                // Resolve max
                if (cSize.maxWidth) { child.res.size.w.max = cSize.maxWidth.resolve(innerWidth); }
                if (cSize.maxHeight) { child.res.size.h.max = cSize.maxHeight.resolve(innerHeight); }

                // Override max if needed
                if (cSize.width.type == Dist::Type::Grow && !set(child.res.size.w.max)) { child.res.size.w.max = 9999999.0f; }
                if (cSize.height.type == Dist::Type::Grow && !set(child.res.size.h.max)) { child.res.size.h.max = 9999999.0f; }

                // Resolve child margin
                child.res.mar.l.val = child.res.mar.l.min = child.res.mar.l.max = cMargin.left.val;
                child.res.mar.r.val = child.res.mar.r.min = child.res.mar.r.max = cMargin.right.val;
                child.res.mar.t.val = child.res.mar.t.min = child.res.mar.t.max = cMargin.top.val;
                child.res.mar.b.val = child.res.mar.b.min = child.res.mar.b.max = cMargin.bottom.val;

                // Resolve child padding
                child.res.pad.l.val = child.res.pad.l.min = child.res.pad.l.max = cPadding.left.val;
                child.res.pad.r.val = child.res.pad.r.min = child.res.pad.r.max = cPadding.right.val;
                child.res.pad.t.val = child.res.pad.t.min = child.res.pad.t.max = cPadding.top.val;
                child.res.pad.b.val = child.res.pad.b.min = child.res.pad.b.max = cPadding.bottom.val;

                float minWidthFromPadding = child.res.pad.l.min + child.res.pad.r.min;
                float minHeightFromPadding = child.res.pad.t.min + child.res.pad.b.min;

                if (!set(child.res.size.w.min)) { child.res.size.w.min = child.layout.size.w.min + minWidthFromPadding; }
                if (!set(child.res.size.h.min)) { child.res.size.h.min = child.layout.size.h.min + minHeightFromPadding; }

                // If no set val, get from min
                if (!set(child.res.size.w.val)) { child.res.size.w.val = child.res.size.w.min; }
                if (!set(child.res.size.h.val)) { child.res.size.h.val = child.res.size.h.min; }

                bool test = true;
            }

            // Measure layout max prior to grow
            //--------------------------------------------------

            bool testb = true;

            if (computed.style.alignment.direction == Axis::Vertical) {

                // Measure val
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
                        row.size.w.val = std::max(row.size.w.val, member->res.getOuter(Axis::Horizontal));
                        row.size.h.val += member->res.getOuter(Axis::Vertical);
                    }

                    layout.size.w.val += row.size.w.val;
                    layout.size.h.val = std::max(layout.size.h.val, row.size.h.val);
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
                        row.size.w.max = std::max(row.size.w.max, member->res.getMaxOuter(Axis::Horizontal));
                        row.size.h.max += member->res.getMaxOuter(Axis::Vertical);
                    }

                    layout.size.w.max += row.size.w.max;
                    layout.size.h.max = std::max(layout.size.h.max, row.size.h.max);
                }
            }

            else {

                // Measure val
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
                        row.size.w.val += member->res.getOuter(Axis::Horizontal);
                        row.size.h.val = std::max(row.size.h.val, member->res.getOuter(Axis::Vertical));
                    }

                    layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                    layout.size.h.val += row.size.h.val;
                }

                // Measure max
                for (Row& row : layout.rows) {

                    for (Element* member : row.members) {
                        row.size.w.max += member->res.getMaxOuter(Axis::Horizontal);
                        row.size.h.max = std::max(row.size.h.max, member->res.getMaxOuter(Axis::Vertical));
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

            // Measure layout val after grow
            //--------------------------------------------------

            layout.size.w.val = -0.0f;
            layout.size.h.val = -0.0f;

            for (Row& row : layout.rows) {

                row.size.w.val = -0.0f;
                row.size.h.val = -0.0f;

                for (Element* member : row.members) {
                    row.size.w.val += member->res.getOuter(Axis::Horizontal);
                    row.size.h.val = std::max(row.size.h.val, member->res.getOuter(Axis::Vertical));
                }

                layout.size.w.val = std::max(layout.size.w.val, row.size.w.val);
                layout.size.h.val += row.size.h.val;
            }

            bool test = true;
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
            layout.rect.w = layout.size.w.val;
            layout.rect.h = layout.size.h.val;

            // Resolve row dimensions
            for (Row& row : layout.rows) {
                
                //row.size.clamp();
                row.rect.w = row.size.w.val;
                row.rect.h = row.size.h.val;

                // Resolve member dimensions
                for (Element* member : row.members) {
                    //member->res.size.clamp();
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

            if (computed.style.alignment.direction == Axis::Vertical) {

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

                        member->rect.x = member->res.mar.l.val + row.rect.x;
                        member->rect.y = member->res.mar.t.val + row.rect.y + runningY;

                        runningY += member->rect.h + member->res.mar.t.val + member->res.mar.b.val;

                        // Apply relative positions
                        //--------------------------------------------------

                        if (member->res.pos.l.val != -0.0f) { member->rect.x += member->res.pos.l.val; }
                        if (member->res.pos.r.val != -0.0f) { member->rect.x += member->res.pos.r.val; }
                        if (member->res.pos.t.val != -0.0f) { member->rect.y += member->res.pos.t.val; }
                        if (member->res.pos.b.val != -0.0f) { member->rect.y += member->res.pos.b.val; }
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

            if (!this->dirty) {

                this->dirty = true;
                e.causedRefresh = true;
                shared->dirtyElements.push_back(this);
            }
    
            // Propagate upwards
            if (parent && !parent->dirty) {
                parent->refresh(e);
            }
        }

        virtual void mouseDown(Event& e) {

            // Mouse down event means we are a drag target
            if (!targetFlags.drag) {
                targetFlags.drag = true;
                if (computed.hasDragStyle) { refresh(e); }
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
                if (computed.hasDragStyle) { refresh(e); }
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
            if (computed.style.cursor != Cursor::Unset) {
                e.mouse.cursor = computed.style.cursor;
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
                if (computed.hasHoverStyle) { refresh(e); }
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
                if (computed.hasHoverStyle) { refresh(e); }
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