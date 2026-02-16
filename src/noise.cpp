#include "noise.h"
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <random>

// Permutation table
static std::vector<int> p;

// Init function
static void initNoise() {
    if (!p.empty()) return;
    p.resize(512);
    // Initialize with 0..255
    for(int i=0; i<256; i++) p[i] = i;
    
    // Shuffle
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(p.begin(), p.begin() + 256, g);
    
    // Duplicate
    for(int i=0; i<256; i++) p[256+i] = p[i];
}

static float fade(float t) { 
    return t * t * t * (t * (t * 6 - 15) + 10); 
}

static float lerp(float t, float a, float b) { 
    return a + t * (b - a); 
}

static float grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

// Basic 3D Perlin Noise Implementation
// References: Ken Perlin's Improved Noise (2002)
float noise3D(float x, float y, float z) {
    // Ensure permutation table is initialized
    if (p.empty()) initNoise();
    
    // Find unit cube that contains the point
    int X = (int)floor(x) & 255;
    int Y = (int)floor(y) & 255;
    int Z = (int)floor(z) & 255;
    
    // Find relative x,y,z of point in cube
    x -= floor(x);
    y -= floor(y);
    z -= floor(z);
    
    // Compute fade curves for x,y,z (smoother interpolation)
    float u = fade(x);
    float v = fade(y);
    float w = fade(z);
    
    // Hash coordinates of the 8 cube corners
    int A = p[X]+Y, AA = p[A]+Z, AB = p[A]+1+Z;
    int B = p[X+1]+Y, BA = p[B]+Z, BB = p[B]+1+Z;
    
    // Add blended results from 8 corners of the cube
    // lerp() interpolates between gradients calculated at each corner
    // grad() computes dot product of gradient vector and distance vector
    return lerp(w, lerp(v, lerp(u, grad(p[AA], x, y, z),
                                   grad(p[BA], x-1, y, z)),
                           lerp(u, grad(p[AB], x, y-1, z),
                                   grad(p[BB], x-1, y-1, z))),
                   lerp(v, lerp(u, grad(p[AA+1], x, y, z-1),
                                   grad(p[BA+1], x-1, y, z-1)),
                           lerp(u, grad(p[AB+1], x, y-1, z-1),
                                   grad(p[BB+1], x-1, y-1, z-1))));
}

/**
 * Curl Noise Implementation
 * 
 * Computes a Divergence-Free velocity field by taking the curl of a vector potential field.
 * Formula: v = curl(psi) = (dPsi_z/dy - dPsi_y/dz, dPsi_x/dz - dPsi_z/dx, dPsi_y/dx - dPsi_x/dy)
 * 
 * Why Curl Noise?
 * Standard Perlin noise is not divergence-free, meaning it acts like a compressible gas (has sinks and sources).
 * Fire/Smoke is an incompressible fluid. Curl noise guarantees div(v) = 0, creating realistic
 * swirling vortices and fluid-like motion without solving Navier-Stokes equations.
 * 
 * @param pos Position to sample noise at
 * @param time Time value for animation
 * @return Velocity vector (divergence-free)
 */
Vec3 curlNoise(Vec3 pos, float time) {
    float eps = 0.1f; // Epsilon for finite difference differentiation
    
    // Helper to sample potential field (uncorrelated noise in x,y,z)
    // We use offsets (100.0, 200.0) to treat the scalar noise as 3 independent components
    auto potential = [&](float x, float y, float z) -> Vec3 {
        return {
            noise3D(x, y, z + time),
            noise3D(x + 100.0f, y + 100.0f, z + time), // Offset for decorrelation
            noise3D(x + 200.0f, y + 200.0f, z + time)
        };
    };
    
    // Finite Difference Method to approximate partial derivatives
    // We use Central Difference: f'(x) approx (f(x+h) - f(x-h)) / 2h
    
    // Partial derivatives with respect to x (d/dx)
    Vec3 p_dx_p = potential(pos.x + eps, pos.y, pos.z);
    Vec3 p_dx_m = potential(pos.x - eps, pos.y, pos.z);
    Vec3 d_dx = (p_dx_p - p_dx_m) * (1.0f / (2.0f * eps));
    
    // Partial derivatives with respect to y (d/dy)
    Vec3 p_dy_p = potential(pos.x, pos.y + eps, pos.z);
    Vec3 p_dy_m = potential(pos.x, pos.y - eps, pos.z);
    Vec3 d_dy = (p_dy_p - p_dy_m) * (1.0f / (2.0f * eps));
    
    // Partial derivatives with respect to z (d/dz)
    Vec3 p_dz_p = potential(pos.x, pos.y, pos.z + eps);
    Vec3 p_dz_m = potential(pos.x, pos.y, pos.z - eps);
    Vec3 d_dz = (p_dz_p - p_dz_m) * (1.0f / (2.0f * eps));
    
    // Compute Curl: ( dPsi_z/dy - dPsi_y/dz, dPsi_x/dz - dPsi_z/dx, dPsi_y/dx - dPsi_x/dy )
    return {
        d_dy.z - d_dz.y,
        d_dz.x - d_dx.z,
        d_dx.y - d_dy.x
    };
}
