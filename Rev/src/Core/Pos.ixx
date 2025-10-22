module;

#include <cmath>
#include <dbg.hpp>

export module Rev.Core.Pos;

export namespace Rev::Core {

    // A position
    struct Pos {

        float x = 0;
        float y = 0;

        Pos() = default;
        Pos(float x, float y) : x(x), y(y) {}

        // Explicitly define copy/move constructors
        Pos(const Pos& other) = default;
        Pos& operator=(const Pos& other) = default;
        
        Pos(Pos&& other) noexcept = default;
        Pos& operator=(Pos&& other) noexcept = default;

        ~Pos() = default; // Ensures proper cleanup

        static Pos Invalid() {
            Pos pos = Pos(std::nan(""), std::nan(""));
            return pos;
        }

        static Pos fromAngle(float angle) {
            return Pos(
                cos(angle),
                sin(angle)
            );
        }

        // Simply return whether Pos has been set
        inline operator bool() const { return !(this->nan()); }

        // Comparator operators based on magnitude
        inline bool operator==(const Pos& other) const { return this->distanceTo(other) < 1e-3; }
        inline bool operator!=(const Pos& other) const { return !(*this == other); }
        inline bool operator<(const Pos& other) const { return this->pythag() < other.pythag(); }
        inline bool operator<=(const Pos& other) const { return this->pythag() <= other.pythag(); }
        inline bool operator>(const Pos& other) const { return this->pythag() > other.pythag(); }
        inline bool operator>=(const Pos& other) const { return this->pythag() >= other.pythag(); }

        // Arithmetic with Pos
        inline Pos operator+(const Pos& other) const { return Pos(x + other.x, y + other.y); }
        inline Pos operator-(const Pos& other) const { return Pos(x - other.x, y - other.y); }
        inline Pos operator*(const Pos& other) const { return Pos(x * other.x, y * other.y); }
        inline Pos operator/(const Pos& other) const { return Pos(x / other.x, y / other.y); }

        // Compound assignment with Pos
        inline Pos& operator+=(const Pos& other) { x += other.x; y += other.y; return *this; }
        inline Pos& operator-=(const Pos& other) { x -= other.x; y -= other.y; return *this; }
        inline Pos& operator*=(const Pos& other) { x *= other.x; y *= other.y; return *this; }
        inline Pos& operator/=(const Pos& other) { x /= other.x; y /= other.y; return *this; }

        // Arithmetic with scalar
        inline Pos operator+(float scalar) const { return Pos(x + scalar, y + scalar); }
        inline Pos operator-(float scalar) const { return Pos(x - scalar, y - scalar); }
        inline Pos operator*(float scalar) const { return Pos(x * scalar, y * scalar); }
        inline Pos operator/(float scalar) const { return Pos(x / scalar, y / scalar); }

        // Compound assignment with scalar
        inline Pos& operator+=(float scalar) { x += scalar; y += scalar; return *this; }
        inline Pos& operator-=(float scalar) { x -= scalar; y -= scalar; return *this; }
        inline Pos& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
        inline Pos& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }

        // Simple operations
        inline Pos setNan() { x = std::nan(""); y = std::nan(""); return *this; }
        inline bool nan() const { return (std::isnan(x) || std::isnan(y)); }
        inline bool isClose(Pos& other, float thresh = 1e-3) { return this->distanceTo(other) < thresh; }
        inline float pythag() const { return sqrt(x*x + y*y); }
        inline float distanceTo(const Pos& pos) const { return (pos - *this).pythag();  }
        inline float angle() const { return atan2(y, x); }
        inline float dot(const Pos& other) const { return x * other.x + y * other.y; }
        inline float cross(const Pos& other) const { return x * other.y - y * other.x; }
        inline Pos centerTo(const Pos& pos) const { return (*this + pos) / 2.f; }
        inline Pos& normalize() { *this /= pythag(); return *this; }
        inline Pos normalized() { return (*this) / this->pythag(); }

        void print() {
            dbg("Pos: { %2f, %2f }", x, y);
        }

        inline float angleTo(const Pos& pos) const {
            return atan2(
                x * pos.y - y * pos.x,
                x * pos.x + y * pos.y
            );
        }

        inline void rotate(float angle) {

            float cosAngle = cos(angle);
            float sinAngle = sin(angle);

            float newX = x * cosAngle - y * sinAngle;
            float newY = x * sinAngle + y * cosAngle;

            x = newX;
            y = newY;
        }

        // Return a rotated copy of the position
        
        inline Pos rotated(float angle) const {
            
            float cosAngle = cos(angle);
            float sinAngle = sin(angle);

            float newX = x * cosAngle - y * sinAngle;
            float newY = x * sinAngle + y * cosAngle;

            return Pos(newX, newY);
        }

        inline float normCross(const Pos& other) const {

            float crossResult = this->cross(other);
            crossResult /= (this->pythag() * other.pythag());
            
            return crossResult;
        }

        inline Pos normal() {
            return Pos(-y, x).normalized();
        }

        inline void round() {
            x = std::round(x);
            y = std::round(y);
        }

        // Rotate by 90 deg
        inline Pos swapAxis() { return Pos(y, x); }

        inline void reflect(Pos& a, Pos& b) {

            // Direction vector of the line
            float dx = b.x - a.x;
            float dy = b.y - a.y;

            // Vector from a to this point
            float px = this->x - a.x;
            float py = this->y - a.y;

            // Dot product of (px, py) and (dx, dy)
            float dot = px * dx + py * dy;

            // Length squared of the direction vector
            float lenSq = dx * dx + dy * dy;

            // Scale factor for projection
            float scale = dot / lenSq;

            // Projection point on the line
            float projX = a.x + scale * dx;
            float projY = a.y + scale * dy;

            // Reflect the point about the line
            this->x = 2 * projX - this->x;
            this->y = 2 * projY - this->y;
        }

        // Return reflected copy
        inline Pos reflected(Pos& a, Pos& b) {

            Pos newPos = *this;
            newPos.reflect(a, b);

            return newPos;
        }
    };

    inline Pos operator+(float scalar, const Pos& pos) { return Pos(pos.x + scalar, pos.y + scalar); }
    inline Pos operator-(float scalar, const Pos& pos) { return Pos(pos.x - scalar, pos.y - scalar); }
    inline Pos operator*(float scalar, const Pos& pos) { return Pos(pos.x * scalar, pos.y * scalar); }
    inline Pos operator/(float scalar, const Pos& pos) { return Pos(pos.x / scalar, pos.y / scalar); }
};