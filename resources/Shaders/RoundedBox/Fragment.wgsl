// Fragment
//--------------------------------------------------

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) fragPos: vec2<f32>,
};

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

    return vec4<f32>(1, 1, 1, 1);
}