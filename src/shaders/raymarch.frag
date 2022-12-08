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
// https://www.shadertoy.com/view/lss3zr

#define MAX_MARCHING_STEPS 128
#define MAX_SHADOW_MARCHING_STEPS 64
#define EPSILON 0.01
#define NEAR_DEPTH 0.1
#define FAR_DEPTH 1000.0
#define GROUND_COLOR vec3(0.58, 0.29, 0)
#define SPHERE_COLOR vec3(1, 0, 0)

#define MAX_CLOUD_MARCHING_STEPS 32
#define MAX_CLOUD_LIGHT_MARCHING_STEPS 16

struct HitPacket {
	vec3 position;
	vec3 normal;
	vec3 cloudColor;
	float depth;
};

float dot2( in vec2 v ) { return dot(v,v); }
float dot2( in vec3 v ) { return dot(v,v); }
float ndot( in vec2 a, in vec2 b ) { return a.x*b.x - a.y*b.y; }

float random(vec2 st) {
	return fract(sin(dot(st.xy, vec2(12.9898,78.233)))* 43758.5453123);
}

float hash(float n)
{
    return fract(sin(n) * 43758.5453);
}

float noise(in vec3 x)
{
    vec3 p = floor(x);
    vec3 f = fract(x);
    
    f = f * f * (3.0 - 2.0 * f);
    
    float n = p.x + p.y * 57.0 + 113.0 * p.z;
    
    float res = mix(mix(mix(hash(n +   0.0), hash(n +   1.0), f.x),
                        mix(hash(n +  57.0), hash(n +  58.0), f.x), f.y),
                    mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
                        mix(hash(n + 170.0), hash(n + 171.0), f.x), f.y), f.z);
    return res;
}

float fbm(vec3 p)
{
    float f;
    f  = 10.000 * noise(p); 
	p *= 2.02;
    f  = 5.000 * noise(p); 
	p *= 2.02;
    f += 2.50 * noise(p); 
	p *= 2.03;
    f += 1.250 * noise(p);
	p *= 2.04;
    f += 0.625 * noise(p);
    return f;
}

// Inigo Quilez's SDF functions
float opUnion(float d1, float d2) { return min(d1,d2); }
float opSubtraction(float d1, float d2) { return max(-d1,d2); }
float opIntersection(float d1, float d2) { return max(d1,d2); }

float opSmoothUnion( float d1, float d2, float k ) {
    float h = clamp( 0.5 + 0.5*(d2-d1)/k, 0.0, 1.0 );
    return mix( d2, d1, h ) - k*h*(1.0-h); }

float opSmoothSubtraction( float d1, float d2, float k ) {
    float h = clamp( 0.5 - 0.5*(d2+d1)/k, 0.0, 1.0 );
    return mix( d2, -d1, h ) + k*h*(1.0-h); }

float opSmoothIntersection( float d1, float d2, float k ) {
    float h = clamp( 0.5 - 0.5*(d2-d1)/k, 0.0, 1.0 );
    return mix( d2, d1, h ) + k*h*(1.0-h); }

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

float opRep(vec3 p, vec3 c)
{
    vec3 q = mod(p+0.5*c,c)-0.5*c;
    return boxSDF(q, vec3(1));
}

float cloudSDF(vec3 position) {
	return 0.1 - length(position) * 0.05 + 0.04 * fbm(position * 0.3 + vec3(ubo.time*0.001, 10, 0));
}

// Compute the scene SDF. This was messier than anticipated...
float sceneSDF(vec3 position) {
	float time = 0.005 * ubo.time;
	float ground = sphereSDF(position - vec3(0.0, 0.0, -100000.0), 99998.0);

	float csg_scene = opUnion(sphereSDF(position - vec3(2.0, 0.0, 0.0), 1),
		opUnion(boxSDF(position - vec3(6.0, 8.0, 0.0), vec3(2, 2, 2)),
		opUnion(torusSDF(position - vec3(0, 15, 8), vec2(5, 0.3)), 
		opUnion(opUnion(
			boxSDF(position - vec3(-2, 8, 0), vec3(2, 2, 1)), sphereSDF(position - vec3(-2, 8, 1.0 + sin(time)), 1)),
		opUnion(opSubtraction(
			boxSDF(position - vec3(-7, 8, 0), vec3(2, 2, 1)), sphereSDF(position - vec3(-7, 8, 1.0 + sin(time)), 1)),
		opUnion(opSubtraction(
			sphereSDF(position - vec3(-12, 8, 1.0 + sin(time)), 1), boxSDF(position - vec3(-12, 8, 0), vec3(2, 2, 1))),
		opUnion(opIntersection(
			boxSDF(position - vec3(-17, 8, 0), vec3(2, 2, 1)), sphereSDF(position - vec3(-17, 8, 1.0 + sin(time)), 1)),
		frameSDF(position - vec3(15, 20, 5), vec3(8, 6, 4), 0.3)
		)))))));

	float smooth_ops = 
		opUnion(
		opUnion(
		opUnion(
			opSmoothUnion(
				boxSDF(position - vec3(-2, 12, 0), vec3(2, 2, 1)), 
				sphereSDF(position - vec3(-2, 12, 1.0 + sin(time)), 1), 0.5),
			opSmoothSubtraction(
				boxSDF(position - vec3(-7, 12, 0), vec3(2, 2, 1)), 
				sphereSDF(position - vec3(-7, 12, 1.0 + sin(time)), 1), 1.0)),
			opSmoothSubtraction(
				sphereSDF(position - vec3(-12, 12, 1.0 + sin(time)), 1),
				boxSDF(position - vec3(-12, 12, 0), vec3(2, 2, 1)),
				0.5
				)),
			opSmoothIntersection(
				boxSDF(position - vec3(-17, 12, 0), vec3(2, 2, 1)), 
				sphereSDF(position - vec3(-17, 12, 1.0 + sin(time)), 1),
				0.5
				));

	float csg_cylinders = opUnion(opUnion(
		cylinderXSDF(position - vec3(12, 0, 2), 3, 2), 
		cylinderYSDF(position - vec3(12, 0, 2), 3, 2)),
		cylinderZSDF(position - vec3(12, 0, 2), 3, 2));

	float csg_obj = opSubtraction(csg_cylinders,
		opIntersection(sphereSDF(position - vec3(12, 0, 2), 3), boxSDF(position - vec3(12, 0, 2), vec3(2.5)))
		);

	float csg_infinite = opIntersection(
		boxSDF(position, vec3(200)),
			opSubtraction(
				boxSDF(position, vec3(25)), 
				opRep(position, vec3(20))
			)
		);

	return opUnion(opUnion(opUnion(opUnion(csg_scene, csg_obj), smooth_ops), csg_infinite), ground);
}

vec3 estimateNormal(vec3 p) {
	// approximate the normal by computing the gradient on x y z
    return normalize(vec3(
        sceneSDF(vec3(p.x + EPSILON, p.y, p.z)) - sceneSDF(vec3(p.x - EPSILON, p.y, p.z)),
        sceneSDF(vec3(p.x, p.y + EPSILON, p.z)) - sceneSDF(vec3(p.x, p.y - EPSILON, p.z)),
        sceneSDF(vec3(p.x, p.y, p.z  + EPSILON)) - sceneSDF(vec3(p.x, p.y, p.z - EPSILON))
    ));
}

HitPacket raymarch(vec3 pos, vec3 rayDir) {
	float depth = NEAR_DEPTH;
	HitPacket hit = HitPacket(vec3(0), vec3(0), vec3(0, 0, 0), FAR_DEPTH);

	vec3 cloudColorAcc = vec3(0);
	vec3 cloudPos = vec3(-20, -10, 2);
    float rayStepMax = 40.0;
    float rayStep = rayStepMax / float(MAX_CLOUD_MARCHING_STEPS);
    float rayLightStepMax = 20.0;
    float rayLightStep = rayLightStepMax / float(MAX_CLOUD_LIGHT_MARCHING_STEPS);
    
    vec3 sun_direction = normalize(vec3(1.0, 0.0, 0.0));
	vec3 lightPos = cloudPos;
    float absorption = 100.0;
    float lightTransmittance = 1.0;
	float transmittance = 1.0;

	for (int i = 0; i < MAX_MARCHING_STEPS; i++) {
		vec3 position = pos + rayDir * depth;

		float newDepth = sceneSDF(position);
		depth += newDepth;

		if (newDepth < EPSILON) { // successful hit, grab information
			vec3 normal = estimateNormal(position);

			hit.position = position;
			hit.normal = normal;
			hit.depth = depth;
			hit.cloudColor = cloudColorAcc;

			break;
		}
		
		if (depth >= FAR_DEPTH) { // no hit found, terminate
			hit.cloudColor = cloudColorAcc;
			break;
		}

		// March cloud if necessary
		float cloudDensity = cloudSDF(position - cloudPos);
		if (cloudDensity > 0.0) {
		   for (int i = 0; i < MAX_CLOUD_MARCHING_STEPS; i++) {
		   		vec3 cloudPosition = position + rayDir * rayStep * i;

				cloudDensity = cloudSDF(cloudPosition - cloudPos);
				if (cloudDensity < 0) break;

				float tmp = cloudDensity / float(MAX_CLOUD_MARCHING_STEPS);
				transmittance *= 1.0 - (tmp * absorption);

				vec3 lightDir = lightPos - cloudPosition;
				
				if (transmittance > EPSILON) {
				    for (int j = 0; j < MAX_CLOUD_LIGHT_MARCHING_STEPS; j++) {
						//vec3 light_position = cloudPosition + sun_direction * rayLightStep * j;
						vec3 light_position = cloudPosition + lightDir * rayLightStep * j;
	
				        float densityLight = cloudSDF(light_position);
				        if (densityLight > 0.0) {
				            lightTransmittance *= 1.0 - (densityLight / float(MAX_CLOUD_LIGHT_MARCHING_STEPS) * absorption);
				        }
				        
				        if (lightTransmittance <= EPSILON) break;
				    }
				    
				    // Add ambient + light scattering color
					float opacity = 30.0;
					float opacityl = 50.0;
					vec3 cloudColor = vec3(1.0);
					vec3 lightColor = vec3(0.0, 1.0, 0);

					//color += vec3(12, 0, 3), vec3(0.2), 10.0);

				    float k = opacity * tmp * transmittance;
				    float kl = opacityl * tmp * transmittance * lightTransmittance;
				    cloudColorAcc += cloudColor * k + lightColor * kl * 2;

				}
			}
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
		float h = sceneSDF(rayOrigin + rayDir*t);
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

		float d = sceneSDF(position + normal * h);
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

float BeersLaw(float dist, float absorption) {
	return exp(-dist * absorption);
}

/*
vec4 raymarchCloud(vec3 pos, vec3 rayDir) {
    float rayStepMax = 40.0;
    float rayStep = rayStepMax / float(MAX_CLOUD_MARCHING_STEPS);
    float rayLightStepMax = 20.0;
    float rayLightStep = rayLightStepMax / float(MAX_CLOUD_LIGHT_MARCHING_STEPS);
    
    vec3 sun_direction = normalize(vec3(1.0, 0.0, 0.0));
    float absorption = 100.0;
    float transmittance = 1.0;
    float lightTransmittance = 1.0;
	float opacity = 50.0;
	float opacityl = 30.0;
	vec3 cloudColor = vec3(1.0, 0.75, 0.79);
	vec3 lightColor = vec3(1.0, 0.75, 0.79);
    
    vec4 color = vec4(0.0);
    
    for (int i = 0; i < MAX_CLOUD_MARCHING_STEPS; i++) {
		vec3 position = pos + rayDir * rayStep * i;

        float density = cloudSDF(position);
		float tmp = density / float(MAX_CLOUD_MARCHING_STEPS);
        
        if (density > 0.0) {
            transmittance *= 1.0 - (tmp * absorption);
            
            if (transmittance <= EPSILON) break;
            
            for (int j = 0; j < MAX_CLOUD_LIGHT_MARCHING_STEPS; j++)
            {
				vec3 light_position = position + sun_direction * rayLightStep * j;

                float densityLight = cloudSDF(light_position);
                if (densityLight > 0.0)
                {
                    lightTransmittance *= 1.0 - (densityLight / float(MAX_CLOUD_LIGHT_MARCHING_STEPS) * absorption);
                }
                
                if (lightTransmittance <= EPSILON) break;
            }
            
            // Add ambient + light scattering color
            float k = opacity * tmp * transmittance;
            float kl = opacityl * tmp * transmittance * lightTransmittance;
            color += cloudColor * k + lightColor * kl;
        }
    }
    
    return color;
}*/

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

	HitPacket hit = raymarch(ubo.eye, rayDir);
	if (hit.depth == FAR_DEPTH) {
		vec3 sky_color_1 = vec3(235, 206, 255) / 255.0;
		vec3 sky_color_2 = vec3(135, 206, 255) / 255.0;

		color = mix(sky_color_1, sky_color_2, uvs.y) + hit.cloudColor;
	} else { // shading!
	  color += AddLight(hit, rayDir, lightPos1, lightColor1, 10.0);
	  color += AddLight(hit, rayDir, lightPos2, lightColor2, 15.0);
	  color += AddLight(hit, rayDir, lightPos3, lightColor3, 20.0);
	  color += AddLight(hit, rayDir, vec3(-20, 10, 3), vec3(1, 1, 1), 20.0);
	  color += AddLight(hit, rayDir, vec3(12, 0, 3), vec3(0.2), 10.0);
	  color += AddLight(hit, rayDir, vec3(-20, -10, 2), vec3(0, 1, 0), 20.0);

	  // ambient occlusion
      float occlusion = AmbientOcclusionAaltonen(hit.position, hit.normal);
	  float ambient = clamp(0.5+0.5*hit.normal.y, 0.0, 1.0);
      color += ambient * occlusion * vec3(0.04,0.04,0.1);

	}

	color *= BeersLaw(hit.depth, 1 / 512.0);

	color += hit.cloudColor;

	outFragColor = vec4(color, 1.0);
}