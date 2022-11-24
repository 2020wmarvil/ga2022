#version 450

layout (location = 0) in vec2 inUVs;

layout (location = 0) out vec4 outFragColor;

#define MAX_MARCHING_STEPS 10
#define EPSILON 0.1
#define NEAR_DEPTH 0.1
#define FAR_DEPTH 1000.0

float sphereSDF(vec3 p, float r) {
    return length(p) - r;
}

float sceneSDF(vec3 position)
{
	return sphereSDF(position - vec3(0.0, 0.0, 5.0), 1);
}

float raymarch()
{
	vec3 eye = vec3(0, 0, 0);

	vec3 viewRayDirection = vec3(inUVs.xy * 2 - vec2(1, 1), 1);

	float depth = NEAR_DEPTH;
	for (int i = 0; i < MAX_MARCHING_STEPS; i++) {
		float dist = sceneSDF(eye + depth * viewRayDirection);
		depth += dist;

		if (dist < EPSILON) {
		    return depth;
		}
		
		if (depth >= FAR_DEPTH) {
			return FAR_DEPTH;
		}
	}

	return FAR_DEPTH;
}

void main()
{
	vec3 color = vec3(raymarch() / FAR_DEPTH);

	outFragColor = vec4(color, 1.0);
}