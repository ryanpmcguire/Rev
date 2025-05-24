module;

#include <webgpu/wgpu.h>

export module Topology;

export namespace WebGpu {

    enum class Topology {
        PointList = WGPUPrimitiveTopology_PointList,
        LineList = WGPUPrimitiveTopology_LineList,
        LineStrip = WGPUPrimitiveTopology_LineStrip,
        TriangleList = WGPUPrimitiveTopology_TriangleList,
        TriangleStrip = WGPUPrimitiveTopology_TriangleStrip,
    };
};