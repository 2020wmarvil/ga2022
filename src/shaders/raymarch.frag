#version 450

layout (location = 0) in vec2 inUVs;

layout (location = 0) out vec4 outFragColor;

// Resources:
// https://jamie-wong.com/2016/07/15/ray-marching-signed-distance-functions/
// https://iquilezles.org/articles/distfunctions/
// https://michaelwalczyk.com/blog-ray-marching.html

#define MAX_MARCHING_STEPS 10
#define EPSILON 0.1
#define NEAR_DEPTH 0.1
#define FAR_DEPTH 1000.0

struct HitPacket {
	vec3 position;
	vec3 normal;
	float depth;
};

float sphereSDF(vec3 p, float r) {
    return length(p) - r;
}

float sceneSDF(vec3 position)
{
	return sphereSDF(position - vec3(0.0, 0.0, 2.0), 1);
}

vec3 estimateNormal(vec3 p) {
	// approximate the normal by computing the gradient on x y z
    return normalize(vec3(
        sceneSDF(vec3(p.x + EPSILON, p.y, p.z)) - sceneSDF(vec3(p.x - EPSILON, p.y, p.z)),
        sceneSDF(vec3(p.x, p.y + EPSILON, p.z)) - sceneSDF(vec3(p.x, p.y - EPSILON, p.z)),
        sceneSDF(vec3(p.x, p.y, p.z  + EPSILON)) - sceneSDF(vec3(p.x, p.y, p.z - EPSILON))
    ));
}

HitPacket raymarch()
{
	vec3 eye = vec3(0, 0, 0);
	vec3 viewRayDirection = vec3(inUVs.xy * 2 - vec2(1, 1), 1);
	float depth = NEAR_DEPTH;

	HitPacket hit = HitPacket(vec3(0), vec3(0), FAR_DEPTH);

	for (int i = 0; i < MAX_MARCHING_STEPS; i++) {
		vec3 position = eye + depth * viewRayDirection;
		float dist = sceneSDF(position);
		depth += dist;

		if (dist < EPSILON) { // successful hit, grab information
			vec3 normal = estimateNormal(position);

			hit.position = position;
			hit.normal = normal;
			hit.depth = depth;

			break;
		}
		
		if (depth >= FAR_DEPTH) { // no hit found, terminate
			break;
		}
	}

	return hit;
}

void main()
{
	vec3 color = vec3(0);

	HitPacket hit = raymarch();
	if (hit.depth == FAR_DEPTH) {
		color = vec3(135, 206, 255) / 255.0; // sky/clear color
	} else {
		color = hit.normal;
	}

	outFragColor = vec4(color, 1.0);
}