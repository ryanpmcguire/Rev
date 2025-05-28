// Global time
struct GlobalTime { time: u32, t2: u32, t3: u32, t4: u32 };

// Global time
@group(0) @binding(0)
var<uniform> globalTime : GlobalTime;

// Animated transform
//--------------------------------------------------

// Transform matrix
@group(1) @binding(0) var<uniform> transformA : mat4x4<f32>;
@group(1) @binding(1) var<uniform> transformB : mat4x4<f32>;
@group(1) @binding(2) var<storage, read_write> transformInterp : mat4x4<f32>;
@group(1) @binding(3) var<storage, read> transform : mat4x4<f32>;

// Animated box data
//--------------------------------------------------

struct BoxData {
    rect: vec4<f32>,
    radius: vec4<f32>,
    color: vec4<f32>,
    time: vec4<u32>
};

@group(2) @binding(0) var<uniform> boxA : BoxData;
@group(2) @binding(1) var<uniform> boxB : BoxData;
@group(2) @binding(2) var<storage, read_write> boxInterp : BoxData;
@group(2) @binding(3) var<storage, read> box : BoxData;

// Compute
//--------------------------------------------------

@compute @workgroup_size(1)
fn cs_main(@builtin(global_invocation_id) global_id: vec3<u32>) {
    
    // Copy boxA into boxInterp
    boxInterp.rect = boxA.rect;
    boxInterp.radius = boxA.radius;
    boxInterp.color = boxA.color;
    boxInterp.time = boxA.time;

    // Copy transformA into transformInterp
    transformInterp = transformA;
}


// Vertex
//--------------------------------------------------

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) fragPos: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {

    // Define quad in clockwise triangles:
    // 0: top-left     (tl)
    // 1: top-right    (tr)
    // 2: bottom-left  (bl)
    // 3: bottom-left  (bl)
    // 4: top-right    (tr)
    // 5: bottom-right (br)

    // Unpack box.rect
    let rectPos = box.rect.xy;
    let rectSize = box.rect.zw;

    // Compute corners
    let l = rectPos.x;
    let r = rectPos.x + rectSize.x;
    let t = rectPos.y;
    let b = rectPos.y + rectSize.y;

    var pos: vec2<f32>;
    switch vertexIndex {
        case 0u: { pos = vec2<f32>(l, t); } // top-left
        case 1u: { pos = vec2<f32>(r, t); } // top-right
        case 2u: { pos = vec2<f32>(l, b); } // bottom-left
        case 3u: { pos = vec2<f32>(l, b); } // bottom-left
        case 4u: { pos = vec2<f32>(r, t); } // top-right
        case 5u: { pos = vec2<f32>(r, b); } // bottom-right
        default: { pos = vec2<f32>(0.0); }
    }

    var out: VertexOutput;
    out.fragPos = pos;
    out.position = transform * vec4<f32>(pos, 0.0, 1.0);
    return out;
}

// Fragment
//--------------------------------------------------

fn roundedBoxSDF(p: vec2<f32>, halfSize: vec2<f32>, radius: vec4<f32>) -> f32 {

    let r = mix(
        mix(radius.x, radius.w, select(1.0, 0.0, p.x > 0.0)),
        mix(radius.y, radius.z, select(1.0, 0.0, p.x > 0.0)),
        select(1.0, 0.0, p.y > 0.0)
    );

    let d = abs(p) - (halfSize - vec2<f32>(r));

    return length(max(d, vec2<f32>(0.0))) - r;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {

    let halfSize = box.rect.zw * 0.5;
    let center = box.rect.xy + halfSize;
    let localPos = in.fragPos - center;

    // Rounded box edge mask
    let sdf = roundedBoxSDF(localPos, halfSize, box.radius);
    let aa = 0.75;
    let edgeAlpha = smoothstep(aa, -aa, sdf); // 1.0 inside shape, fades out at edge

    // Time-based decay
    //let t = f32(globalTime.time) * 0.001; // convert ms to seconds
    //let decay = exp(-0.693 * t);          // 1-second half-life

    return vec4<f32>(box.color.rgb, box.color.a * edgeAlpha);
}