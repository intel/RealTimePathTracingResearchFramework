// https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial-Part-2
struct HitInfo {
	float4 color_dist;
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes {
	float2 bary;
};

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> output : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure scene : register(t0);

// View params buffer
cbuffer ViewParams : register(b0) {
	float4 cam_pos;
	float4 cam_du;
	float4 cam_dv;
	float4 cam_dir_top_left;
}

float linear_to_srgb(float x) {
	if (x <= 0.0031308f) {
		return 12.92f * x;
	}
	return 1.055f * pow(x, 1.f / 2.4f) - 0.055f;
}

[shader("raygeneration")] 
void RayGen() {
	// Initialize the ray payload
	HitInfo payload;
	payload.color_dist = float4(0, 0, 0, 1);

	// Get the location within the dispatched 2D grid of work items
	// (often maps to pixels, so this could represent a pixel coordinate).
	uint2 pixel = DispatchRaysIndex().xy;
	float2 dims = float2(DispatchRaysDimensions().xy);

	// The example traces an orthographic view essentially (or NDC "projection")
	float2 d = pixel / dims;
	RayDesc ray;
	ray.Origin = cam_pos.xyz;
	ray.Direction = normalize(d.x * cam_du.xyz + d.y * cam_dv.xyz + cam_dir_top_left.xyz);
	ray.TMin = 0;
	ray.TMax = 1e20f;
	
	const int ray_type = 0;
	const int num_ray_types = 2;
	TraceRay(scene, 0, 0xff, ray_type, 2, ray_type, ray, payload);

	output[pixel] = float4(linear_to_srgb(payload.color_dist.r),
		linear_to_srgb(payload.color_dist.g),
		linear_to_srgb(payload.color_dist.b), 1.f);
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload) {
	payload.color_dist = float4(0, 0, 0, 0);
}

[shader("miss")]
void AOMiss(inout HitInfo payload : SV_RayPayload) {
	payload.color_dist = float4(1, 0, 0, 0);
}

StructuredBuffer<float3> vertices : register(t0, space1);
StructuredBuffer<uint3> indices : register(t1, space1);

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) {
	uint3 idx = indices[PrimitiveIndex()];
	float3 va = vertices[idx.x];
	float3 vb = vertices[idx.y];
	float3 vc = vertices[idx.z];
	float3 n = normalize(cross(vb - va, vc - va));
	float3 c = (n + float3(1, 1, 1)) * 0.5;
	payload.color_dist = float4(c, RayTCurrent());
}

[shader("closesthit")]
void AOHit(inout HitInfo payload, Attributes attrib) {
	payload.color_dist = float4(0, 0, 1, RayTCurrent());
}
