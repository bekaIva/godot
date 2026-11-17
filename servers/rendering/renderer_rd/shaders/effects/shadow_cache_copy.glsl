#[vertex]

#version 450

#VERSION_DEFINES

layout(location = 0) out vec2 uv_interp;

void main() {
	vec2 base_arr[4] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0));
	uv_interp = base_arr[gl_VertexIndex];
	gl_Position = vec4(uv_interp * 2.0 - 1.0, 0.0, 1.0);
}

#[fragment]

#version 450

#VERSION_DEFINES

// A directional light's cached cascade, its depth copied texel for texel
// into the cascade's place in the shadow atlas (the draw's region).

layout(location = 0) in vec2 uv_interp;

layout(set = 0, binding = 0) uniform sampler2D source_depth;

void main() {
	ivec2 size = textureSize(source_depth, 0);
	ivec2 texel = clamp(ivec2(uv_interp * vec2(size)), ivec2(0), size - 1);
	gl_FragDepth = texelFetch(source_depth, texel, 0).r;
}
