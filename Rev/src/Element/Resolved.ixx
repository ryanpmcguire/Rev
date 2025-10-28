module;

#include <bit>
#include <algorithm>

export module Rev.Element.Resolved;

import Rev.Element.Style;

namespace Rev::Element {

    export struct ResolvedDim {

        // Returns if value is set
        inline bool set(float& f) {

            bool isSet = std::bit_cast<uint32_t>(f) != 0x80000000;

            return isSet;
        };

        // -0.0f means "unset" or "unspecified"
        float val = -0.0f, min = -0.0f, max = -0.0f;
        bool growable = false;
        bool fit = true;

        void setAbs(Dist& newVal, Dist& newMin, Dist& newMax) {

            // Set new val/min/max if type is absolute
            if (newVal.type == Dist::Type::Abs) { val = newVal.val; growable = false; }
            if (newMin.type == Dist::Type::Abs) { min = newMin.val; }
            if (newMax.type == Dist::Type::Abs) { max = newMax.val; }

            // Any set value means min/max are also set
            if (set(val) && !min) { min = val; }
            if (set(val) && !max) { max = val; }
        }

        void setRel(Dist& newVal, Dist& newMin, Dist& newMax, float& compare, float& compareMin, float& compareMax) {

            // Set new val/min/max if type is absolute
            if (newVal.type == Dist::Type::Rel && compare > 0.0f) { val = newVal.val * compare; growable = false; }
            if (newMin.type == Dist::Type::Rel && compareMin > 0.0f) { min = newMin.val * compareMin; }
            if (newMax.type == Dist::Type::Rel && compareMax > 0.0f) { max = newMax.val * compareMax; }

            // Handle unset minimum
            if (!set(min) && newVal.type == Dist::Type::Rel) {
                min = newVal.val * compareMin;
            }

            if (!set(max) && newVal.type == Dist::Type::Rel) {
                max = newVal.val * compareMax;
            }

            // Any set value means min/max are also set
            if (set(val) && !min) { min = val; }
            if (set(val) && !max) { max = val; }
        }

        void setNonFlex(Dist& newVal, Dist& newMin, Dist& newMax, float& compareMin, float& compareMax) {

            // Set new val
            switch (newVal.type) {
                case (Dist::Type::Abs): { val = newVal.val; fit = false; break; }
                case (Dist::Type::Rel): { min = newVal.val * compareMin; max = newVal.val * compareMax; fit = false; break; }
            }

            // Set new min (a set minimum means do not fit)
            switch (newMin.type) {
                case (Dist::Type::Abs): { min = newMin.val; break; }
                case (Dist::Type::Rel): { min = newMin.val * compareMin; fit = false; break; }
            }

            // Set new val
            switch (newMax.type) {
                case (Dist::Type::Abs): { max = std::min(newMax.val, compareMax); break; }
                case (Dist::Type::Rel): { max = newMax.val * compareMax; break; }
            }

            // Any set value means min/max are also set
            if (set(val) && !min) { min = val; }
            if (set(val) && !max) { max = val; }
        }

        // This is a special case, since min/max can't grow - they can only be set as abs/rel
        void setGrow(Dist& newVal, float& maxGrow) {

            if (newVal.type == Dist::Type::Grow) {

                // We set this dimension as growable
                growable = true;
                fit = false;

                if (maxGrow && !max) { max = maxGrow; }

                // If there is no existing max, we set it to a large value
                if (!max) { max = 999999.0f; }
            }
        }

        // Limit dimension based on resolved min/max values
        void clamp() {
            
            // Restrict val to min/max values
            if (max && val > max) { val = max; }
            if (min && val < min) { val = min; }

            // After clamping, we can simply set min/max
            //if (val) { max = val; min = val; }
        }

        // Determine whether this dimension is capable of growing
        bool canGrow() {

            if (!growable) { return false; }    // If not grow, we can never grow
            if (!max) { return true; }          // If no max, we can always grow
            if (val < max) { return true; }     // If there's space left, we can grow
            
            growable = false;                   // Once false, always false
            return false;                       // Default is false
        }

        // Dimension is offered an amount to grow by, it may take less but not more
        float grow(float share) {

            float take = share;
            if (max) { take = std::min(share, max - val); }

            val += take;

            return take;
        }
    };

    export struct ResolvedSize {

        ResolvedDim w, h;

        void setAbs(Size& size) {
            w.setAbs(size.width, size.minWidth, size.maxWidth);
            h.setAbs(size.height, size.minHeight, size.maxHeight);
        }

        void setRel(Size& size, float& innerWidth, float& innerHeight, float& minInnerWidth, float& minInnerHeight, float& maxInnerWidth, float& maxInnerHeight) {
            w.setRel(size.width, size.minWidth, size.maxWidth, innerWidth, minInnerWidth, maxInnerWidth);
            h.setRel(size.height, size.minHeight, size.maxHeight, innerHeight, minInnerHeight, maxInnerHeight);
        }

        void setNonFlex(Size& size, float& minInnerWidth, float& minInnerHeight, float& maxInnerWidth, float& maxInnerHeight) {
            w.setNonFlex(size.width, size.minWidth, size.maxWidth, minInnerWidth, maxInnerWidth);
            h.setNonFlex(size.height, size.minHeight, size.maxHeight, minInnerHeight, maxInnerHeight);
        }

        void setGrow(Size& size, float& max) {
            w.setGrow(size.width, max);
            h.setGrow(size.height, max);
        }

        // Clamp all dims
        void clamp() {
            w.clamp(); h.clamp();
        }

        // Get maximum along axis
        float getMax(Axis axis) {
            if (axis == Axis::Horizontal) { return w.max; }
            if (axis == Axis::Vertical) { return h.max; }
        }

        // Get minimum along axis
        float getMin(Axis axis) {
            if (axis == Axis::Horizontal) { return w.min; }
            if (axis == Axis::Vertical) { return h.min; }
        }

        // Get number of growable dims
        int canGrow(Axis axis) {

            if (axis == Axis::Horizontal) { return w.canGrow(); }
            if (axis == Axis::Vertical) { return h.canGrow(); }

            return w.canGrow() + h.canGrow();
        }
    };

    export struct ResolvedLrtb {

        ResolvedDim l, r, t, b;

        void setAbs(LrtbStyle& lrtb) {

            l.setAbs(lrtb.left, lrtb.minLeft, lrtb.maxLeft);
            r.setAbs(lrtb.right, lrtb.minRight, lrtb.maxRight);
            t.setAbs(lrtb.top, lrtb.minTop, lrtb.maxTop);
            b.setAbs(lrtb.bottom, lrtb.minBottom, lrtb.maxBottom);
        }

        void setRel(LrtbStyle& lrtb, float& width, float& height, float& minWidth, float& minHeight, float& maxWidth, float& maxHeight) {

            l.setRel(lrtb.left, lrtb.minLeft, lrtb.maxLeft, width, minWidth, maxWidth);
            r.setRel(lrtb.right, lrtb.minRight, lrtb.maxRight, width, minWidth, maxWidth);
            t.setRel(lrtb.top, lrtb.minTop, lrtb.maxTop, height, minHeight, maxHeight);
            b.setRel(lrtb.bottom, lrtb.minBottom, lrtb.maxBottom, height, minHeight, maxHeight);
        }

        void setNonFlex(LrtbStyle& lrtb, ResolvedSize& size) {
            l.setNonFlex(lrtb.left, lrtb.minLeft, lrtb.maxLeft, size.w.val, size.h.max);
            r.setNonFlex(lrtb.right, lrtb.minRight, lrtb.maxRight, size.w.val, size.h.max);
            t.setNonFlex(lrtb.top, lrtb.minTop, lrtb.maxTop, size.h.val, size.h.max);
            b.setNonFlex(lrtb.bottom, lrtb.minBottom, lrtb.maxBottom, size.h.val, size.h.max);
        }

        void setGrow(LrtbStyle& lrtb, float& max) {
            l.setGrow(lrtb.left, max);
            r.setGrow(lrtb.right, max);
            t.setGrow(lrtb.top, max);
            b.setGrow(lrtb.bottom, max);
        }
        
        // Clamp all dims
        void clamp() {
            l.clamp(); r.clamp();
            t.clamp(); b.clamp();
        }

        // Get maximum along axis
        float getMax(Axis axis) {

            // Get maximum in horizontal direction
            if (axis == Axis::Horizontal) {
                if (!l.max || !r.max) { return -0.0f; }
                return (l.max + r.max);
            }

            // Get maximum in vertical direction
            if (axis == Axis::Vertical) {
                if (!t.max || !b.max) { return -0.0f; }
                return (t.max + b.max);
            }
        }

        // Get minimum along axis
        float getMin(Axis axis) {

            // Get minimum in horizontal direction
            if (axis == Axis::Horizontal) {
                if (!l.min && !r.min) { return 0; }
                return (l.min + r.min);
            }

            // Get minimum in vertical direction
            if (axis == Axis::Vertical) {
                if (!t.min && !b.min) { return 0; }
                return (t.min + b.min);
            }
        }

        // Get number of growable dims
        int canGrow(Axis axis) {

            if (axis == Axis::Horizontal) { return l.canGrow() + r.canGrow(); }
            if (axis == Axis::Vertical) { return t.canGrow() + b.canGrow(); }

            return (
                l.canGrow() + r.canGrow() +
                t.canGrow() + b.canGrow()
            );
        }
    };

    export struct Resolved {

        ResolvedSize size;
        ResolvedLrtb mar;
        ResolvedLrtb pad;
        ResolvedLrtb pos;

        // Clamp all dims
        void clamp() {
            size.clamp();
            mar.clamp();
            pad.clamp();
        }

        // Get outer dimension along axis
        float getOuter(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.val + mar.l.val + mar.r.val; }
            if (axis == Axis::Vertical) { return size.h.val + mar.t.val + mar.b.val; }
        }

        // Get max outer dimension along axis
        float getMaxOuter(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.max + mar.l.max + mar.r.max; }
            if (axis == Axis::Vertical) { return size.h.max + mar.t.max + mar.b.max; }
        }

        // Get max outer dimension along axis
        float getMinOuter(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.min + mar.l.min + mar.r.min; }
            if (axis == Axis::Vertical) { return size.h.min + mar.t.min + mar.b.min; }
        }

        // Get inner dimension along axis
        float getInner(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.val - pad.l.val - pad.r.val; }
            if (axis == Axis::Vertical) { return size.h.val - pad.t.val - pad.b.val; }
        }

        // Get max inner dimension along axis
        float getMaxInner(Axis axis) {
            if (axis == Axis::Horizontal) { return std::max(size.w.max - pad.l.min - pad.r.min, 0.0f); }
            if (axis == Axis::Vertical) { return std::max(size.h.max - pad.t.min - pad.b.min, 0.0f); }
        }

        // Get min inner dimension along axis
        float getMinInner(Axis axis) {
            if (axis == Axis::Horizontal) { return size.w.min - pad.l.max - pad.r.max; }
            if (axis == Axis::Vertical) { return size.h.min - pad.t.max - pad.b.max; }
        }

        // Get maximum along axis
        float getMax(Axis axis) {
            
            float maxSize = size.getMax(axis);
            float maxMar = mar.getMax(axis);

            if (!maxSize || !maxMar) { return 0; }

            return maxSize + maxMar;
        }

        // Get minimum along axis
        float getMin(Axis axis) {
            
            float minSize = size.getMin(axis);
            float minMar = mar.getMin(axis);

            if (!minSize && !minMar) { return 0; }

            return minSize + minMar;
        }

        // Get number of growable dims
        int canGrow(Axis axis) {
            return (
                size.canGrow(axis) +
                mar.canGrow(axis) +
                pad.canGrow(axis)
            );
        }

        float grow(float share, Axis axis) {

            float take = 0;
            
            // Grow horizontal dimensions
            if (axis == Axis::Horizontal) {
                if (size.w.canGrow()) { take += size.w.grow(share); }
                if (mar.l.canGrow()) { take += mar.l.grow(share); }
                if (mar.r.canGrow()) { take += mar.r.grow(share); }
            }

            // Grow vertical dimensions
            else if (axis == Axis::Vertical) {
                if (size.h.canGrow()) { take += size.h.grow(share); }
                if (mar.t.canGrow()) { take += mar.t.grow(share); }
                if (mar.b.canGrow()) { take += mar.b.grow(share); }
            }

            return take;
        }
    };
}