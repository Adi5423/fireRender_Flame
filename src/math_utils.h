#pragma once
#include <cmath>

// Simple 3D Vector structure used throughout the simulation
struct Vec3 { float x,y,z; };

// ---- Vector Arithmetic Operators ----

// Vector addition: a + b
inline Vec3 operator+(Vec3 a,Vec3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }

// Vector subtraction: a - b
inline Vec3 operator-(Vec3 a,Vec3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }

// Scalar multiplication: a * s
inline Vec3 operator*(Vec3 a,float s){ return {a.x*s,a.y*s,a.z*s}; }

// ---- Vector Math Utilities ----

// Normalize a vector to unit length (length = 1.0)
inline Vec3 normalize(Vec3 v){
    float l = sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return {v.x/l,v.y/l,v.z/l};
}

// Compute Cross Product of two vectors
// Result is perpendicular to both input vectors
inline Vec3 cross(Vec3 a,Vec3 b){
    return {
        a.y*b.z-a.z*b.y,
        a.z*b.x-a.x*b.z,
        a.x*b.y-a.y*b.x
    };
}

// Compute 2D Euclidean distance from origin (XZ plane)
// Used for radial flame calculations
inline float length2D(float x, float z){
    return sqrtf(x*x + z*z);
}
