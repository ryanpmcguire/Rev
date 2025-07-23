module;

#include <string>
#include <vector>

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

        Style style;
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

        // Comptue style
        void computeStyle(Event& e) {

            // Apply other styles, then own style
            computed.style.apply(styles);
            computed.style.apply(style);

            //for (Element* child : children) {
            //    child->computeStyle(e);
            //}
        }
        
        // Compute attributes
        virtual void computePrimitives(Event& e) {

            for (Element* child : children) {
                child->computePrimitives(e);
            }
        }

        virtual void draw(Event& e) {

            for (Element* child : children) {
                child->draw(e);
            }
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

        // TOP DOWN
        // Resolve dims that don't flex (px/pct)
        void resolveNonFlexDims() {

            // Reset resolved and rect
            res = Resolved();
            rect = Rect();

            // Get maximum inner width/height of parent
            float maxInnerWidth = parent->res.getMaxInner(Axis::Horizontal);
            float maxInnerHeight = parent->res.getMaxInner(Axis::Vertical);

            // Set dimensions abs or relative to maximum occupiable dims
            res.size.setNonFlex(style.size, maxInnerWidth, maxInnerHeight);
            res.mar.setNonFlex(style.margin, res.size);
            res.pad.setNonFlex(style.padding, res.size);

            res.size.setGrow(style.size);
            res.mar.setGrow(style.margin);
            res.pad.setGrow(style.padding);

            // Inherit maxima from parent if none
            if (!res.size.w.max) { res.size.w.max = maxInnerWidth; }
            if (!res.size.h.max) { res.size.h.max = maxInnerHeight; }
        }

        // Resolve layout (bottom up)
        // This is by far the slowest function
        void resolveLayout() {

            // Note - for some reason we MUST do this, even with no children?
            // Answer: because of the section after resolving the layout
            // Requires children
            //if (children.empty()) {
            //    return;
            //}

            // Reset layout
            layout = Layout();
            Row row = Row();

            // Wrap children
            //--------------------------------------------------

            // The maximum size this element can possibly contain
            float maxInnerWidth = res.getMaxInner(Axis::Horizontal);
            float maxInnerHeight = res.getMaxInner(Axis::Vertical);

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

            if(layout.size.w.min + res.pad.l.min + res.pad.r.min > res.size.w.min) {
                res.size.w.min = layout.size.w.min + res.pad.l.min + res.pad.r.min;
                res.size.w.max = std::max(res.size.w.min, res.size.w.max);
            }

            if(layout.size.h.min + res.pad.t.min + res.pad.b.min > res.size.h.min) {
                res.size.h.min = layout.size.h.min + res.pad.t.min + res.pad.b.min;
                res.size.h.max = std::max(res.size.h.min, res.size.h.max);
            }

            // Inherit maxima from children
            //--------------------------------------------------

            if (res.size.w.fit) { res.size.w.max = layout.size.w.max + res.pad.l.val + res.pad.r.val; }
            if (res.size.h.fit) { res.size.h.max = layout.size.h.max + res.pad.t.val + res.pad.b.val; }
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
        }

        // Resolve grow/shrink
        void resolveFlexDims() {

            // Requires children
            if (children.empty()) {
                return;
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
            }
        }

        // Resolve final positions (top down)
        void resolveRects() {

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

            layoutOffsetX += center(res.getInner(Axis::Horizontal), layout.rect.w, style.alignment.horizontal);
            layoutOffsetY += center(res.getInner(Axis::Vertical), layout.rect.h, style.alignment.vertical);

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
                }

                runningY += row.rect.h;
            }
        }

        // Event
        //--------------------------------------------------

        virtual void refresh(Event& e) {

            // Set self
            this->dirty = true;
            e.causedRefresh = true;
            topLevelDetails->dirtyElements.push_back(this);

            // Propagate upwards
            if (parent && !parent->dirty) { parent->refresh(e); }
        }

        virtual bool contains(Event& e) {
            return rect.contains(e.mouse.pos);
        }

        virtual void mouseDown(Event& e) {

            for (Element* child : children) {
                if (child->contains(e)) { child->mouseDown(e); }
                if (!e.propagate) { break; }
            }
        }
    };
}