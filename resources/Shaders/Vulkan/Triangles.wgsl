// Vertex output type carrying color to the fragment shader
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
};

struct GlobalTime {
    time: i64
};

// Global time
@group(0) @binding(0)
var<uniform> globalTime : GlobalTime;

// Transform matrix uniform
@group(1) @binding(0)
var<uniform> transform : mat4x4<f32>;

@vertex
fn vs_main(
    @location(0) pos: vec2<f32>,
    @location(1) color: vec4<f32>
) -> VertexOutput {

    var out: VertexOutput;

    out.position = transform * vec4<f32>(pos, 0.0, 1.0);
    out.color = color;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return in.color;
}