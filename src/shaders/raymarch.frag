#version 450

layout (location = 0) in vec2 inUVs;

layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform UBO 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
	vec3 forward;
	vec3 eye;
} ubo;

// Resources:
// https://jamie-wong.com/2016/07/15/ray-marching-signed-distance-functions/
// https://iquilezles.org/articles/distfunctions/
// https://michaelwalczyk.com/blog-ray-marching.html

#define MAX_MARCHING_STEPS 100
#define EPSILON 0.1
#define NEAR_DEPTH 0.1
#define FAR_DEPTH 1000.0
#define GROUND_COLOR vec3(0.58, 0.29, 0)
#define SPHERE_COLOR vec3(1, 0, 0)

struct HitPacket {
	vec3 position;
	vec3 normal;
	vec3 color;
	float depth;
};

float sphereSDF(vec3 p, float r) {
    return length(p) - r;
}

HitPacket sceneSDF(vec3 position)
{
	float ground = sphereSDF(position - vec3(0.0, 0.0, -10000.0), 9998.0);
	float sphere = sphereSDF(position - vec3(2.0, 0.0, 0.0), 1);

	HitPacket hit;
	if (ground < sphere) {
		hit.depth = ground;
		hit.color = GROUND_COLOR;
	} else {
		hit.depth = sphere;
		hit.color = SPHERE_COLOR;
	}

	return hit;
}

vec3 estimateNormal(vec3 p) {
	// approximate the normal by computing the gradient on x y z
    return normalize(vec3(
        sceneSDF(vec3(p.x + EPSILON, p.y, p.z)).depth - sceneSDF(vec3(p.x - EPSILON, p.y, p.z)).depth,
        sceneSDF(vec3(p.x, p.y + EPSILON, p.z)).depth - sceneSDF(vec3(p.x, p.y - EPSILON, p.z)).depth,
        sceneSDF(vec3(p.x, p.y, p.z  + EPSILON)).depth - sceneSDF(vec3(p.x, p.y, p.z - EPSILON)).depth
    ));
}

HitPacket raymarch()
{
	vec3 eye = ubo.eye;
	vec3 camForward = ubo.forward;
	vec3 camRight = normalize(cross(vec3(0, 0, 1), camForward));
	vec3 camUp = normalize(cross(camForward, camRight));

	vec2 centeredUVs = inUVs.xy * 2 - vec2(1, 1);
	vec3 viewRayDirection = normalize(camForward + camRight * centeredUVs.x + camUp * centeredUVs.y);

	float depth = NEAR_DEPTH;

	HitPacket hit = HitPacket(vec3(0), vec3(0), vec3(0, 0, 0), FAR_DEPTH);

	for (int i = 0; i < MAX_MARCHING_STEPS; i++) {
		vec3 position = eye + depth * viewRayDirection;

		HitPacket newHit = sceneSDF(position);
		float dist = newHit.depth;

		depth += dist;

		if (dist < EPSILON) { // successful hit, grab information
			vec3 normal = estimateNormal(position);

			hit.position = position;
			hit.normal = normal;
			hit.depth = depth;
			hit.color = newHit.color;

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
	
	vec3 lightPos = vec3(0, 0, 5);

	HitPacket hit = raymarch();
	if (hit.depth == FAR_DEPTH) {
		color = vec3(135, 206, 255) / 255.0; // sky/clear color
	} else {
		color = hit.color * vec3(dot(hit.normal, normalize(lightPos - hit.position)));
	}

	outFragColor = vec4(color, 1.0);
}