#version 430 core

in vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D imgTex;

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;
    float opacity;
    float tileCountX;
    float tileCountY;
    float pad;
};

void main() {

    outColor   = texture(imgTex, fragUV);
    outColor.a *= opacity;

    // Draw tile grid overlay when tile counts are set
    if (tileCountX > 0.0 && tileCountY > 0.0) {
        vec2  cell = fract(fragUV * vec2(tileCountX, tileCountY));
        // ~2 screen pixels wide relative to the tile cell size
        float lwX  = 2.0 / w;
        float lwY  = 2.0 / h;
        if (cell.x < lwX || cell.x > (1.0 - lwX) ||
            cell.y < lwY || cell.y > (1.0 - lwY)) {
            outColor = vec4(0.0, 0.88, 1.0, 0.9);
        }
    }
}
