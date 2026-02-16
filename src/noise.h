#pragma once
#include "math_utils.h"

// Simple 3D Perlin noise implementation
float noise3D(float x, float y, float z);

// Curl noise for divergence-free turbulence
Vec3 curlNoise(Vec3 pos, float time);
