#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(binding = 3, set = 0, rgba8) uniform image2D envmap;

void main()
{
    hitValue = imageLoad(envmap, ivec2(gl_WorldRayDirectionEXT.xy*64+64)).xyz;
}
