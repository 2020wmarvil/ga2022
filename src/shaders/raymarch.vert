#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUVs;

layout (binding = 0) uniform UBO 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
} ubo;

layout (location = 0) out vec2 outUVs;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
	outUVs = inUVs;
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
}
