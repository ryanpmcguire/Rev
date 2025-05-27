// Transform matrix
@group(0) @binding(0)
var<uniform> transform : mat4x4<f32>;

// Global time
struct GlobalTime { time: u32, t2: u32, t3: u32, t4: u32 };

// Global time
@group(1) @binding(0)
var<uniform> globalTime : GlobalTime;

struct BoxData {
    rect: vec4<f32>,
    radius: vec4<f32>,
    color: vec4<f32>,
    time: vec4<u32>
};

@group(2) @binding(0) var<uniform> box : BoxData;
@group(2) @binding(1) var<uniform> boxB : BoxData;

// Vertex
//--------------------------------------------------

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) fragPos: vec2<f32>,
};

@vertex
fn vs_main(@location(0) pos: vec2<f32>) -> VertexOutput {

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