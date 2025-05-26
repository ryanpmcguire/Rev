// Structs and layout
//--------------------------------------------------

// Transform matrix
@group(0) @binding(0)
var<uniform> transform : mat4x4<f32>;

struct BoxData {
    rect_x: f32, rect_y: f32,
    rect_w: f32, rect_h: f32,
    rad_tl: f32, rad_tr: f32,
    rad_bl: f32, rad_br: f32,
    fill_r: f32, fill_g: f32, fill_b: f32, fill_a: f32
};

@group(1) @binding(0)
var<uniform> box : BoxData;

// Vertex
//--------------------------------------------------

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) fragPos: vec2<f32>,
};

@vertex
fn vs_main(@location(0) pos: vec2<f32>) -> VertexOutput {
    var out: VertexOutput;
    out.position = transform * vec4<f32>(pos, 0.0, 1.0);
    out.fragPos = pos;
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
    
    let rect = vec4<f32>(box.rect_x, box.rect_y, box.rect_w, box.rect_h);
    let radius = vec4<f32>(box.rad_tl, box.rad_tr, box.rad_br, box.rad_bl);
    let fillColor = vec4<f32>(box.fill_r, box.fill_g, box.fill_b, box.fill_a);

    let halfSize = rect.zw * 0.5;
    let center = rect.xy + halfSize;
    let localPos = in.fragPos - center;

    let dist = roundedBoxSDF(localPos, halfSize, radius);

    let aa = 1.0;
    let alpha = smoothstep(aa, -aa, dist);

    return vec4<f32>(fillColor.rgb, fillColor.a * alpha);
}
