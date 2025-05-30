// Bind interpolated transform matrix (group 1)
@group(1) @binding(2) var<storage, read> transform : mat4x4<f32>;

// Bind interpolated box data (group 2)
struct BoxData {
    rect: vec4<f32>,
    radius: vec4<f32>,
    color: vec4<f32>,
    time: vec4<u32>
};
@group(2) @binding(2) var<storage, read> box : BoxData;

// Output structure for fragment stage
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) fragPos: vec2<f32>, // world-space position before transform
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {

    // Extract box position + size
    let pos = box.rect.xy;
    let size = box.rect.zw;

    // Compute quad corners
    var verts = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0), // top-left
        vec2<f32>(1.0, 0.0), // top-right
        vec2<f32>(0.0, 1.0), // bottom-left
        vec2<f32>(0.0, 1.0), // bottom-left
        vec2<f32>(1.0, 0.0), // top-right
        vec2<f32>(1.0, 1.0)  // bottom-right
    );

    let local = verts[vertexIndex] * size + pos;

    var out: VertexOutput;
    out.fragPos = local; // Keep untransformed for SDF
    out.position = transform * vec4<f32>(local, 0.0, 1.0);
    
    return out;
}
