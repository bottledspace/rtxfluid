#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(location = 0) rayPayloadInEXT struct {
	vec3 color;
	int count;
} payload;
hitAttributeEXT struct {
	vec3 n;
	vec3 i;
	bool inside;
} hit;

void main()
{
	payload.count = payload.count+1;
	if (payload.count > 10) {
		return;
	}

	vec3 R1 = refract(gl_WorldRayDirectionEXT, hit.inside?-hit.n:hit.n, hit.inside?(1.5/1.0):(1.0/1.5));
	traceRayEXT(topLevelAS, 0, 0xff, 0, 0, 0, hit.i+0.01*(hit.inside?hit.n:-hit.n), 0.0, R1, 10.0, 0);
	vec3 color1 = payload.color;

	vec3 R2 = reflect(gl_WorldRayDirectionEXT, hit.n);
	traceRayEXT(topLevelAS, 0, 0xff, 0, 0, 0, hit.i, 0.0, R2, 10.0, 0);
	vec3 color2 = payload.color;
	
	float R = dot(gl_WorldRayDirectionEXT, hit.inside ? hit.n:-hit.n);
	payload.color = 0.9*(color1*(R) + color2*(1-R));
}
