module;

#include <algorithm>

export module Rev.Resolved;

import Rev.Style;

export namespace Rev {

    struct ResolvedDim {

        // -0.0f means "unset" or "unspecified"
        float val = -0.0f, min = -0.0f, max = -0.0f;
        bool growable = false;
        bool fit = true;

        // Set Abs val
        void setAbs(Dist& newVal, Dist& newMin, Dist& newMax) {

            if (newVal.type == Dist::Type::Abs) { val = newVal.val; fit = false; }
            if (newMin.type == Dist::Type::Abs) { min = newMin.val; }
            if (newMax.type == Dist::Type::Abs) { max = newMax.val; }

            // Set all in the case of newVal being Abs
            if (val) {

                val = newVal.val;
                min = newVal.val;
                max = newVal.val;
            }
        }

        // Set Rel to other
        void setRel(Dist& newVal, Dist& newMin, Dist& newMax, float compare) {

            // Set min/max
            if (newMin.type == Dist::Type::Rel) { min = newMin.val * compare; fit = false; }
            if (newMax.type == Dist::Type::Rel) { max = newMax.val * compare; }
            if (newVal.type == Dist::Type::Rel) { val = newVal.val * compare; }

            if (val) {
                min = val;
                max = val;
            }

            // Clamp vals
            //this->clamp();
        }

        void setNonFlex(Dist& newVal, Dist& newMin, Dist& newMax, float& compare) {

            // Set new val
            switch (newVal.type) {
                case (Dist::Type::Abs): { val = newVal.val; fit = false; break; }
                case (Dist::Type::Rel): { val = newVal.val * compare; break; }
            }

            // Set new min (a set minimum means do not fit)
            switch (newMin.type) {
                case (Dist::Type::Abs): { min = newMin.val;break; }
                case (Dist::Type::Rel): { min = newMin.val * compare; fit = false; break; }
            }

            // Set new val
            switch (newMax.type) {
                case (Dist::Type::Abs): { max = std::min(newMax.val, compare); break; }
                case (Dist::Type::Rel): { max = newMax.val * compare; break; }
            }

            // Any set val means min/max are also set
            if (val) {
                min = val;
                max = val;
            }
        }

        // This is a special case, since min/max can't grow - they can only be set as abs/rel
        void setGrow(Dist& newVal) {

            if (newVal.type == Dist::Type::Grow) {

                // We set this dimension as growable
                growable = true;
                fit = false;

                // If there is no existing max, we set it to a large val
                if (!max) { max = 999999.0f; }
            }
        }

        // Basically, "suggest" a val to the dimension (conditional override)
        void suggest(float newVal, float newMin, float newMax, bool newGrowable) {

            //if (!val) { val = newVal; }
            if (!min) { min = newMin; }
            if (!max) { max = newMax; }
            if (!growable) { growable = newGrowable; }
        }

        // Limit dimension based on resolved min/max vals
        void clamp() {
            
            // Restrict val to min/max vals
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

    struct ResolvedSize {

        ResolvedDim w, h;

        // Set Abss
        void setAbs(Size& size) {
            w.setAbs(size.width, size.minWidth, size.maxWidth);
            h.setAbs(size.height, size.minHeight, size.maxHeight);
        }

        // Set Rel to other size
        void setRel(Size& size, float innerWidth, float innerHeight) {
            w.setRel(size.width, size.minWidth, size.maxWidth, innerWidth);
            h.setRel(size.height, size.minHeight, size.maxHeight, innerHeight);
        }

        void setNonFlex(Size& size, float& innerWidth, float& innerHeight) {
            w.setNonFlex(size.width, size.minWidth, size.maxWidth, innerWidth);
            h.setNonFlex(size.height, size.minHeight, size.maxHeight, innerHeight);
        }

        void setGrow(Size& size) {
            w.setGrow(size.width);
            h.setGrow(size.height);
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

    struct ResolvedLrtb {

        ResolvedDim l, r, t, b;

        // Set Abss
        void setAbs(LrtbStyle& lrtb) {
            l.setAbs(lrtb.left, lrtb.minLeft, lrtb.maxLeft);
            r.setAbs(lrtb.right, lrtb.minRight, lrtb.maxRight);
            t.setAbs(lrtb.top, lrtb.minTop, lrtb.maxTop);
            b.setAbs(lrtb.bottom, lrtb.minBottom, lrtb.maxBottom);
        }

        // Set Rel to other size
        void setRel(LrtbStyle& lrtb, ResolvedSize& size) {
            l.setRel(lrtb.left, lrtb.minLeft, lrtb.maxLeft, size.w.val);
            r.setRel(lrtb.right, lrtb.minRight, lrtb.maxRight, size.w.val);
            t.setRel(lrtb.top, lrtb.minTop, lrtb.maxTop, size.h.val);
            b.setRel(lrtb.bottom, lrtb.minBottom, lrtb.maxBottom, size.h.val);
        }

        void setNonFlex(LrtbStyle& lrtb, ResolvedSize& size) {
            l.setNonFlex(lrtb.left, lrtb.minLeft, lrtb.maxLeft, size.w.val);
            r.setNonFlex(lrtb.right, lrtb.minRight, lrtb.maxRight, size.w.val);
            t.setNonFlex(lrtb.top, lrtb.minTop, lrtb.maxTop, size.h.val);
            b.setNonFlex(lrtb.bottom, lrtb.minBottom, lrtb.maxBottom, size.h.val);
        }

        void setGrow(LrtbStyle& lrtb) {
            l.setGrow(lrtb.left);
            r.setGrow(lrtb.right);
            t.setGrow(lrtb.top);
            b.setGrow(lrtb.bottom);
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
                if (!l.max || !r.max) { return 0; }
                return (l.max + r.max);
            }

            // Get maximum in vertical direction
            if (axis == Axis::Vertical) {
                if (!t.max || !b.max) { return 0; }
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

    struct Resolved {

        ResolvedSize size;
        ResolvedLrtb mar;
        ResolvedLrtb pad;

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