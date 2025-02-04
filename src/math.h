#pragma once

// Math helpers.

#include <float.h>
#include <stdbool.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define MICRO_TO_MILLI 1000
#define DEGREES_TO_RADIANS M_PI / 180.0f;

// Determines if two scalar values are nearly equal
// given the limitations of floating point accuracy.
__forceinline bool almost_equalf(float a, float b)
{
	float diff = fabsf(a - b);
	if (diff <= FLT_EPSILON * 1000.0f)
	{
		return true;
	}
	if (diff <= __max(fabsf(a), fabsf(b)) * FLT_EPSILON * 4.0f)
	{
		return true;
	}
	return false;
}

// Linearly interpolate between two floating point values.
// Return the resulting value.
__forceinline float lerpf(float begin, float end, float distance)
{
	return (begin * (1.0f - distance)) + (end * distance);
}

__forceinline float degrees_to_radians(float degrees)
{
	return degrees * (float)DEGREES_TO_RADIANS;
}
