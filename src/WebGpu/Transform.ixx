module;

#include <webgpu/wgpu.h>

export module Transform;

import UniformBuffer;

export namespace WebGpu {

    struct Transform : public UniformBuffer {

        struct Matrix {
            float m[16] = {
                0, 0, 0, 0,    // Identity matrix
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 0
            };
        };

        inline static Matrix matrix;

        static inline float surfaceWidth = 100.0, surfaceHeight = 100.0f;
        
        // Create
        Transform(WGPUDevice device, uint32_t index)
        : UniformBuffer({device, &matrix, sizeof(Matrix), index }) {

        }

        // Destroy
        ~Transform() {

        }

        void compute(uint32_t time) {

            float x = 0, y = 0;
            float w = surfaceWidth;
            float h = surfaceHeight;

            // Build orthographic projection matrix with Y flipped
            matrix.m[0] =  2.0f / w;
            matrix.m[5] = -2.0f / h;
            matrix.m[10] = 1.0f;
            matrix.m[12] = (2.0f * x / w) - 1.0f;
            matrix.m[13] = 1.0f - (2.0f * y / h);
            matrix.m[15] = 1.0f;

            dirty = true;
        }
    };
};