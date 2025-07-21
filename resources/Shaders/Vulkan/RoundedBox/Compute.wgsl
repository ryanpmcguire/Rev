// Global time in milliseconds
struct GlobalTime {
    time: u32,
    t2: u32,
    t3: u32,
    t4: u32
};

@group(0) @binding(0)
var<uniform> globalTime: GlobalTime;

// Interpolated transform (group 1)
@group(1) @binding(0) var<uniform> transformA: mat4x4<f32>;
@group(1) @binding(1) var<uniform> transformB: mat4x4<f32>;
@group(1) @binding(2) var<storage, read_write> transform: mat4x4<f32>;

struct BoxData {
    rect: vec4<f32>,
    radius: vec4<f32>,
    color: vec4<f32>,
    time: vec4<u32>, // x: startTime, y: duration
}

@group(2) @binding(0) var<uniform> boxA: BoxData;
@group(2) @binding(1) var<uniform> boxB: BoxData;
@group(2) @binding(2) var<storage, read_write> box: BoxData;

fn interpolate_mat4(a: mat4x4<f32>, b: mat4x4<f32>, alpha: f32) -> mat4x4<f32> {
    return mat4x4<f32>(
        mix(a[0], b[0], alpha),
        mix(a[1], b[1], alpha),
        mix(a[2], b[2], alpha),
        mix(a[3], b[3], alpha)
    );
}

fn interpolate_box(a: BoxData, b: BoxData, alpha: f32) -> BoxData {
    return BoxData(
        mix(a.rect, b.rect, alpha),
        mix(a.radius, b.radius, alpha),
        mix(a.color, b.color, alpha),
        b.time // preserve latest time/duration
    );
}

@compute @workgroup_size(1)
fn cs_main(@builtin(global_invocation_id) id: vec3<u32>) {

    let now = f32(globalTime.time);
    let start = f32(boxA.time.x);
    let duration = max(f32(boxA.time.y), 0.001);
    let alpha = clamp((now - start) / duration, 0.0, 1.0);

    transform = interpolate_mat4(transformA, transformB, alpha);
    box = interpolate_box(boxA, boxB, alpha);

    //box = boxA;
    //transform = transformA;
}
