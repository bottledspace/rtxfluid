#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT struct {
	vec3 color;
	int count;
} payload;

layout(binding = 3, set = 0, rgba8) uniform image2D envmap;

void main()
{
	ivec2 scale = imageSize(envmap);
	float theta = scale.x*(atan(gl_WorldRayDirectionEXT.x,gl_WorldRayDirectionEXT.z) / 3.14159 + 1.0)/2;
	float phi   = scale.y*(acos(gl_WorldRayDirectionEXT.y) / 3.14159);
    payload.color = imageLoad(envmap, ivec2(theta,scale.y-phi)).bgr;
}
