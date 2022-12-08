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
	float time;
} ubo;

// Resources:
// https://jamie-wong.com/2016/07/15/ray-marching-signed-distance-functions/
// https://iquilezles.org/articles/distfunctions/
// https://michaelwalczyk.com/blog-ray-marching.html
// https://www.shadertoy.com/view/lsKcDD

#define MAX_MARCHING_STEPS 128
#define MAX_SHADOW_MARCHING_STEPS 64
#define EPSILON 0.01
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

float dot2( in vec2 v ) { return dot(v,v); }
float dot2( in vec3 v ) { return dot(v,v); }
float ndot( in vec2 a, in vec2 b ) { return a.x*b.x - a.y*b.y; }

float random(vec2 st) {
	return fract(sin(dot(st.xy, vec2(12.9898,78.233)))* 43758.5453123);
}

// Inigo Quilez's SDF functions
float opUnion(float d1, float d2) { return min(d1,d2); }
float opSubtraction(float d1, float d2) { return max(-d1,d2); }
float opIntersection(float d1, float d2) { return max(d1,d2); }

float sphereSDF(vec3 p, float r) {
    return length(p) - r;
}

float boxSDF(vec3 p, vec3 b)
{
  vec3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

float torusSDF(vec3 p, vec2 t)
{
  vec2 q = vec2(length(p.xz)-t.x,p.y);
  return length(q)-t.y;
}

float frameSDF( vec3 p, vec3 b, float e )
{
       p = abs(p  )-b;
  vec3 q = abs(p+e)-e;
  return min(min(
      length(max(vec3(p.x,q.y,q.z),0.0))+min(max(p.x,max(q.y,q.z)),0.0),
      length(max(vec3(q.x,p.y,q.z),0.0))+min(max(q.x,max(p.y,q.z)),0.0)),
      length(max(vec3(q.x,q.y,p.z),0.0))+min(max(q.x,max(q.y,p.z)),0.0));
}

float cylinderXSDF( vec3 p, float h, float r )
{
  vec2 d = abs(vec2(length(p.yz),p.x)) - vec2(r,h);
  return min(max(d.x,d.y),0.0) + length(max(d,0.0));
}

float cylinderYSDF( vec3 p, float h, float r )
{
  vec2 d = abs(vec2(length(p.xz),p.y)) - vec2(r,h);
  return min(max(d.x,d.y),0.0) + length(max(d,0.0));
}

float cylinderZSDF( vec3 p, float h, float r )
{
  vec2 d = abs(vec2(length(p.xy),p.z)) - vec2(r,h);
  return min(max(d.x,d.y),0.0) + length(max(d,0.0));
}

HitPacket sceneSDF(vec3 position)
{
	float ground = sphereSDF(position - vec3(0.0, 0.0, -10000.0), 9998.0);

	float csg_scene = opUnion(sphereSDF(position - vec3(2.0, 0.0, 0.0), 1),
		opUnion(boxSDF(position - vec3(6.0, 8.0, 0.0), vec3(2, 2, 2)),
		opUnion(torusSDF(position - vec3(0, 8, 8), vec2(5, 0.3)), 
		opUnion(opUnion(
			boxSDF(position - vec3(-2, 8, 0), vec3(2, 2, 1)), sphereSDF(position - vec3(-2, 8, 0.5), 1)),
		opUnion(opSubtraction(
			boxSDF(position - vec3(-7, 8, 0), vec3(2, 2, 1)), sphereSDF(position - vec3(-7, 8, 0.5), 1)),
		opUnion(opSubtraction(
			sphereSDF(position - vec3(-12, 8, 0.5), 1), boxSDF(position - vec3(-12, 8, 0), vec3(2, 2, 1))),
		opUnion(opIntersection(
			boxSDF(position - vec3(-17, 8, 0), vec3(2, 2, 1)), sphereSDF(position - vec3(-17, 8, 0.5), 1)),
		frameSDF(position - vec3(15, 20, 5), vec3(8, 6, 4), 0.3)
		)))))));

	float csg_cylinders = opUnion(opUnion(
		cylinderXSDF(position - vec3(12, 0, 2), 3, 2), 
		cylinderYSDF(position - vec3(12, 0, 2), 3, 2)),
		cylinderZSDF(position - vec3(12, 0, 2), 3, 2));

	float csg_obj = opSubtraction(csg_cylinders,
		opIntersection(sphereSDF(position - vec3(12, 0, 2), 3), boxSDF(position - vec3(12, 0, 2), vec3(2.5)))
		);

	csg_scene = opUnion(csg_scene, csg_obj);

	HitPacket hit;
	if (ground < csg_scene) {
		hit.depth = ground;
		hit.color = GROUND_COLOR;
	} else {
		hit.depth = csg_scene;
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

HitPacket Raymarch(vec3 pos, vec3 rayDir) {
	float depth = NEAR_DEPTH;

	HitPacket hit = HitPacket(vec3(0), vec3(0), vec3(0, 0, 0), FAR_DEPTH);

	for (int i = 0; i < MAX_MARCHING_STEPS; i++) {
		vec3 position = pos + rayDir * depth;

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

// Sebastian Aaltonen's soft shadow function
float SoftShadowAaltonen(vec3 rayOrigin, vec3 rayDir, float minDist, float maxDist) {
	float res = 1.0;
    float t = minDist;
    float ph = 1e10; // big, such that y = 0 on the first iteration
    
    for( int i=0; i<MAX_SHADOW_MARCHING_STEPS; i++) {
		HitPacket hit = sceneSDF(rayOrigin + rayDir*t);
		float h = hit.depth;
		float y = h*h/(2.0*ph);
		float d = sqrt(h*h-y*y);
		res = min( res, 32.0*d/max(0.0,t-y) );
		ph = h;
        t += h;
        
        if( res<0.0001 || t>maxDist ) break;
    }

    res = clamp(res, 0.0, 1.0);
    return res*res*(3.0 - 2.0 * res);
}

float AmbientOcclusionAaltonen(vec3 position, vec3 normal) {
	float occlusion = 0.0;
    float sca = 1.0;

    for(int i=0; i<5; i++) {
        float h = 0.001 + 0.15*float(i)/4.0;

		HitPacket hit = sceneSDF(position + normal * h);
		float d = hit.depth;
        occlusion += (h-d)*sca;
        sca *= 0.95;
    }

    return clamp( 1.0 - 1.5*occlusion, 0.0, 1.0 );    
}

vec3 GetViewRayDir(vec2 uvs) {
	vec3 camForward = ubo.forward;
	vec3 camRight = normalize(cross(vec3(0, 0, 1), camForward));
	vec3 camUp = normalize(cross(camForward, camRight));
	vec2 centeredUVs = uvs * 2.0 - vec2(1, 1);
	return normalize(camForward + camRight * centeredUVs.x + camUp * centeredUVs.y);
}

vec3 AddLight(HitPacket hit, vec3 rayDir, vec3 lightPos, vec3 lightColor, float attenuationRadius) {
	vec3 color = vec3(0.0);

	vec3 lightVector = lightPos - hit.position;
	vec3 lightDir = normalize(lightVector);
	vec3 hal = normalize(lightDir - rayDir);

	// basic lighting
	float diffuse = clamp(dot(hit.normal, lightDir), 0.0, 1.0);
	float specular = pow(clamp(dot(hit.normal, hal), 0.0, 1.0), 16.0) * diffuse * (0.04 + 0.96 * pow(clamp(1.0 + dot(hal,rayDir),0.0,1.0), 5.0));

	// soft shadows!
	float shadow = SoftShadowAaltonen(hit.position + hit.normal * 0.01, lightDir, 0, length(lightVector));
	if (shadow > 0) specular = 0;

	float lightDist = length(lightVector);
	float attenuation = clamp(1.0 - lightDist*lightDist/(attenuationRadius * attenuationRadius), 0.0, 1.0); 
	attenuation *= attenuation;

	return lightColor * attenuation * (1.0 * diffuse * shadow + 12.0 * specular);
}

void main()
{
	vec2 uvs = inUVs;
	uvs.x *= 16.0 / 9.0;

	vec3 rayDir = GetViewRayDir(uvs);

	float time = 0.0002 * ubo.time;
	vec3 lightPos1 = vec3(3*sin(time), 3*cos(time), 2);
	vec3 lightColor1 = vec3(1.0, 0.0, 0.0);
	vec3 lightPos2 = vec3(3*cos(time), 2*sin(-0.5*time), 1);
	vec3 lightColor2 = vec3(0.0, 0.0, 1.0);
	vec3 lightPos3 = vec3(3*cos(-0.5*time), 3*sin(2*time), 2.5);
	vec3 lightColor3 = vec3(0.0, 1.0, 0.0);

	vec3 color = vec3(0);

	HitPacket hit = Raymarch(ubo.eye, rayDir);
	if (hit.depth == FAR_DEPTH) {
		vec3 sky_color_1 = vec3(235, 206, 255) / 255.0;
		vec3 sky_color_2 = vec3(135, 206, 255) / 255.0;

		color = mix(sky_color_1, sky_color_2, uvs.y);
	} else { // shading!
		color += AddLight(hit, rayDir, lightPos1, lightColor1, 10.0);
		color += AddLight(hit, rayDir, lightPos2, lightColor2, 15.0);
		color += AddLight(hit, rayDir, lightPos3, lightColor3, 20.0);
		color += AddLight(hit, rayDir, vec3(-20, 10, 3), vec3(1, 1, 1), 20.0);
		color += AddLight(hit, rayDir, vec3(12, 0, 3), vec3(0.2), 10.0);

		// ambient occlusion
        float occlusion = AmbientOcclusionAaltonen(hit.position, hit.normal);
		float ambient = clamp(0.5+0.5*hit.normal.y, 0.0, 1.0);
        color += ambient * occlusion * vec3(0.04,0.04,0.1);

		// fog
		//color *= exp(-0.0005 * hit.depth * hit.depth * hit.depth);
	}

	outFragColor = vec4(color, 1.0);
}