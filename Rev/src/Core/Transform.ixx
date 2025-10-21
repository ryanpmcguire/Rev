module;

#include <cmath>

export module Rev.Core.Transform;

import Rev.Core.Pos;

export namespace Rev::Core {

    struct Transform  {

        float m[3][3];

        // Constructors
        //--------------------------------------------------
    
        // Represents identity matrix (does nothing)
        static Transform Identity() {
            return { { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} } };
        }
    
        // Represents translation (shifts in space)
        static Transform Translation(float tx, float ty) {
            return { { {1, 0, tx}, {0, 1, ty}, {0, 0, 1} } };
        }
    
        // Represents scale (scales in space)
        static Transform Scale(float sx, float sy) {
            return { { {sx, 0, 0}, {0, sy, 0}, {0, 0, 1} } };
        }
    
        // Represents rotation (rotates in space)
        static Transform Rotation(float rads) {
            float c = std::cos(rads), s = std::sin(rads);
            return { { {c, -s, 0}, {s, c, 0}, {0, 0, 1} } };
        }

        // Represents the inverse of a transform (the opposite)
        static Transform Inverse(const Transform& t) {
            
            Transform inv;
            const float (*m)[3] = t.m; // shorthand pointer for readability
        
            float a = m[0][0], b = m[0][1], c = m[0][2];
            float d = m[1][0], e = m[1][1], f = m[1][2];
            float g = m[2][0], h = m[2][1], i = m[2][2];
        
            // Compute determinant
            float det = a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);
            if (std::fabs(det) < 1e-8f) { return Identity(); }
        
            float invDet = 1.0f / det;
        
            inv.m[0][0] =  (e*i - f*h) * invDet;
            inv.m[0][1] = -(b*i - c*h) * invDet;
            inv.m[0][2] =  (b*f - c*e) * invDet;
        
            inv.m[1][0] = -(d*i - f*g) * invDet;
            inv.m[1][1] =  (a*i - c*g) * invDet;
            inv.m[1][2] = -(a*f - c*d) * invDet;
        
            inv.m[2][0] =  (d*h - e*g) * invDet;
            inv.m[2][1] = -(a*h - b*g) * invDet;
            inv.m[2][2] =  (a*e - b*d) * invDet;
        
            return inv;
        }

        // Operators
        //--------------------------------------------------

        // Multiply transforms (combine their effects)
        Transform operator*(const Transform& o) const {

            Transform r = {};

            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    r.m[i][j] = m[i][0]*o.m[0][j] + m[i][1]*o.m[1][j] + m[i][2]*o.m[2][j];
                }
            }

            return r;
        }
    
        // Apply transform to single point
        Pos operator*(const Pos& p) const {

            float x = m[0][0]*p.x + m[0][1]*p.y + m[0][2];
            float y = m[1][0]*p.x + m[1][1]*p.y + m[1][2];
            float w = m[2][0]*p.x + m[2][1]*p.y + m[2][2];

            if (w != 0.0f) { x /= w; y /= w; }

            return {x, y};
        }

        // Fetchers
        //--------------------------------------------------

        Pos getScale() const {
            
            // Extract linear components
            float a = m[0][0], b = m[0][1];
            float c = m[1][0], d = m[1][1];
        
            // Length of the column vectors gives scale along each axis
            float scaleX = std::sqrt(a * a + c * c);
            float scaleY = std::sqrt(b * b + d * d);
        
            return Pos(scaleX, scaleY);
        }
    };
    
};