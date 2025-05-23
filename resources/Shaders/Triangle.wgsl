@vertex
fn vs_main(@location(0) pos: vec2<f32>) -> @builtin(position) vec4<f32> {
    return vec4<f32>(pos, 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) fragCoord: vec4f) -> @location(0) vec4f {
    let edgeFade = smoothstep(0.0, 1.0, fragCoord.x % 1.0); // example edge fade
    return vec4f(1.0, 1.0, 1.0, edgeFade);
}