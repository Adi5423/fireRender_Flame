#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <cstddef>

/* =================== MATH =================== */
#include "math_utils.h"

/* =================== CAMERA =================== */
Vec3 camPos={0.0f, 0.5f, 3.5f}, camFront={0,0,-1}, camUp={0,1,0};
float yaw=-90,pitch=0;
bool rmb=false, firstMouse=true;
float lastX=640,lastY=360;

void mouse(GLFWwindow*,double x,double y){
    if(!rmb){ firstMouse=true; return; }
    if(firstMouse){ lastX=(float)x; lastY=(float)y; firstMouse=false; }

    float dx=(float)x-lastX, dy=lastY-(float)y;
    lastX=(float)x; lastY=(float)y;

    dx*=0.1f; dy*=0.1f;
    yaw+=dx; pitch+=dy;
    pitch = fmaxf(-89,fminf(89,pitch));

    camFront = normalize({
        cosf(yaw*0.0174533f)*cosf(pitch*0.0174533f),
        sinf(pitch*0.0174533f),
        sinf(yaw*0.0174533f)*cosf(pitch*0.0174533f)
    });
}

/* =================== SHADERS =================== */

// Fullscreen triangle vertex shader
const char* fullscreenVS = R"(
#version 460 core
const vec2 v[3]=vec2[]( vec2(-1,-1), vec2(3,-1), vec2(-1,3) );
out vec2 uv;
void main(){
    gl_Position=vec4(v[gl_VertexID],0,1);
    uv=v[gl_VertexID];
}
)";

// ==========================================
// VOLUMETRIC FLAME RAYMARCHING FRAGMENT SHADER
// ==========================================
const char* flameFS = R"(
#version 460 core
in vec2 uv;
out vec4 fragColor;

uniform float iTime;
uniform vec3  iCamPos;
uniform vec3  iCamFront;
uniform vec3  iCamUp;
uniform float iAspect;
uniform float iFormation;  // 0..1 formation progress

// ---- Noise functions (GPU side) ----

// Hash function for noise
vec3 hash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

// 3D value noise
float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    return mix(mix(mix(dot(hash33(i + vec3(0,0,0)), f - vec3(0,0,0)),
                       dot(hash33(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x),
                   mix(dot(hash33(i + vec3(0,1,0)), f - vec3(0,1,0)),
                       dot(hash33(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
               mix(mix(dot(hash33(i + vec3(0,0,1)), f - vec3(0,0,1)),
                       dot(hash33(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x),
                   mix(dot(hash33(i + vec3(0,1,1)), f - vec3(0,1,1)),
                       dot(hash33(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z);
}

// Fractional Brownian Motion - multi-octave noise
float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for(int i = 0; i < octaves; i++) {
        value += amplitude * noise3D(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

// ---- Flame SDF & density ----

// Flame body: teardrop / elongated cone shape
// The flame is centered at origin, extends upward along Y
float flameSDF(vec3 p) {
    // Flame base at y=0, tip around y=flameHeight
    float flameHeight = 1.8;
    
    // Normalize height [0..1]
    float h = clamp(p.y / flameHeight, 0.0, 1.0);
    
    // Radius profile: wide at base, narrow at tip
    // Uses a smooth curve: wider at bottom, tapers to point
    float baseRadius = 0.18;
    
    // Teardrop profile: r(h) = baseRadius * (1-h)^power * sqrt(1 + h*widening)
    // This creates the characteristic flame shape:
    //  - wide and round at bottom
    //  - narrows smoothly toward top
    //  - slight bulge in middle (combustion zone)
    float taper = pow(1.0 - h, 1.3);
    float bulge = 1.0 + 0.4 * h * (1.0 - h) * 4.0; // bulge peaks at h=0.5
    float radius = baseRadius * taper * bulge;
    
    // Radial distance in XZ plane
    float radialDist = length(p.xz);
    
    // SDF: negative inside, positive outside
    return radialDist - radius;
}

// Compute flame density at a point
// This combines the SDF shape with noise-based turbulence
float flameDensity(vec3 p, float time) {
    float flameHeight = 1.8;
    float h = p.y / flameHeight;
    
    // Outside vertical range = no flame
    if(h < -0.05 || h > 1.1) return 0.0;
    
    // --- Turbulence displacement ---
    // Fire rises, so noise scrolls upward over time
    // This creates the characteristic upward-licking motion
    vec3 noisePos = p;
    noisePos.y -= time * 1.5;  // scroll noise upward (flame rises)
    
    // Multi-scale turbulence
    // Large scale: overall flame sway
    float largeTurb = fbm(noisePos * 2.0 + vec3(0, time*0.3, 0), 3) * 0.12;
    // Medium scale: individual tongue motion
    float medTurb = fbm(noisePos * 5.0 + vec3(time*0.5, 0, time*0.3), 3) * 0.06;
    // Fine scale: small flickering details
    float fineTurb = fbm(noisePos * 12.0 + vec3(0, time*0.8, 0), 2) * 0.02;
    
    // Turbulence increases with height (tip flickers more than base)
    float turbScale = 0.3 + h * 1.5;
    
    // Displace the sample point
    vec3 displaced = p;
    displaced.x += (largeTurb + medTurb + fineTurb) * turbScale;
    displaced.z += (largeTurb - medTurb + fineTurb * 0.5) * turbScale * 0.7;
    
    // Evaluate SDF at displaced position
    float sdf = flameSDF(displaced);
    
    // Convert SDF to density (smooth falloff)
    // Negative SDF = inside flame = positive density
    float density = 1.0 - smoothstep(-0.06, 0.04, sdf);
    
    // Modulate density with noise for internal structure
    float internalNoise = fbm(noisePos * 8.0 + vec3(0, time * 2.0, 0), 3);
    density *= 0.6 + 0.4 * (0.5 + 0.5 * internalNoise);
    
    // Fade density at bottom (flame doesn't start abruptly)
    float bottomFade = smoothstep(-0.02, 0.08, p.y);
    density *= bottomFade;
    
    // Fade at top (flame dissolves)
    float topFade = 1.0 - smoothstep(0.7, 1.0, h);
    density *= topFade;
    
    // Formation: scale density by formation progress
    density *= iFormation;
    
    return max(density, 0.0);
}

// ---- Temperature & Color ----

// Compute temperature based on position within flame
// Returns 0..1 where 1 = hottest (blue/white core), 0 = coolest (dark tips)
float getTemperature(vec3 p, float density, float time) {
    float flameHeight = 1.8;
    float h = clamp(p.y / flameHeight, 0.0, 1.0);
    float radial = length(p.xz);
    
    // Temperature is highest at the core center, near the base
    // and decreases with:
    //  1. Height (convective cooling as gases rise)
    //  2. Radial distance (cooler at edges)
    
    // Height-based cooling: hottest at bottom, coolest at top
    float heightTemp = 1.0 - pow(h, 0.7);
    
    // Radial cooling: hottest at center axis
    float maxR = 0.18 * (1.0 - h * 0.5);
    float radialTemp = 1.0 - smoothstep(0.0, maxR, radial);
    
    // Core temperature
    float temp = heightTemp * (0.4 + 0.6 * radialTemp);
    
    // Blue zone: very base of flame (h < 0.15), inner region
    // In real candles, the blue cone is where complete combustion occurs
    // (methane + O2 → CO2 + H2O, emitting blue light from CH radicals)
    
    // Add slight noise variation for flickering temperature
    vec3 noiseP = p;
    noiseP.y -= time * 1.2;
    float tempNoise = fbm(noiseP * 6.0, 2) * 0.15;
    temp += tempNoise;
    
    return clamp(temp * density, 0.0, 1.0);
}

// Blackbody-inspired flame coloring
// Maps temperature to realistic flame colors
vec3 flameColor(float temp, float height, float radial) {
    // Real candle flame color zones (from reference image):
    //  Bottom:  Blue (CH radical emission, ~1400°C)
    //  Lower:   Blue-white transition
    //  Core:    Bright yellow-white (incandescent soot, ~1200°C)  
    //  Middle:  Yellow-orange
    //  Upper:   Orange-red (cooling soot)
    //  Tip:     Dark red/transparent (dissipating heat)
    
    vec3 color;
    
    // Blue base zone
    if(height < 0.2 && temp > 0.5) {
        float blueStrength = (1.0 - height / 0.2) * smoothstep(0.5, 0.8, temp);
        vec3 blueColor = mix(vec3(0.1, 0.3, 0.9), vec3(0.4, 0.6, 1.0), temp);
        vec3 hotColor = mix(vec3(1.0, 0.7, 0.2), vec3(1.0, 0.95, 0.8), (temp - 0.5) * 2.0);
        color = mix(hotColor, blueColor, blueStrength * 0.6);
    }
    // Hot core (bright yellow-white)
    else if(temp > 0.75) {
        color = mix(vec3(1.0, 0.85, 0.3), vec3(1.0, 0.97, 0.85), (temp - 0.75) * 4.0);
    }
    // Mid flame (yellow-orange)
    else if(temp > 0.5) {
        color = mix(vec3(1.0, 0.55, 0.05), vec3(1.0, 0.85, 0.3), (temp - 0.5) * 4.0);
    }
    // Cooler regions (orange-red)
    else if(temp > 0.25) {
        color = mix(vec3(0.8, 0.2, 0.0), vec3(1.0, 0.55, 0.05), (temp - 0.25) * 4.0);
    }
    // Cooling edges (dark red)
    else if(temp > 0.1) {
        color = mix(vec3(0.3, 0.05, 0.0), vec3(0.8, 0.2, 0.0), (temp - 0.1) * 6.67);
    }
    // Nearly extinguished
    else {
        color = vec3(0.15, 0.02, 0.0) * temp * 10.0;
    }
    
    return color;
}

// ---- Raymarching ----

// Intersect ray with flame bounding volume (cylinder)
// Returns (tNear, tFar) or (-1,-1) if no hit
vec2 intersectFlameVolume(vec3 ro, vec3 rd) {
    // Bounding cylinder: radius=0.5, height=[−0.1, 2.0]
    float cylRadius = 0.5;
    float yMin = -0.1;
    float yMax = 2.0;
    
    // Intersect with infinite cylinder (XZ plane)
    float a = rd.x * rd.x + rd.z * rd.z;
    float b = 2.0 * (ro.x * rd.x + ro.z * rd.z);
    float c = ro.x * ro.x + ro.z * ro.z - cylRadius * cylRadius;
    
    float disc = b * b - 4.0 * a * c;
    if(disc < 0.0) return vec2(-1.0);
    
    float sqrtDisc = sqrt(disc);
    float t0 = (-b - sqrtDisc) / (2.0 * a);
    float t1 = (-b + sqrtDisc) / (2.0 * a);
    
    // Clip to Y planes
    float tYMin0 = (yMin - ro.y) / rd.y;
    float tYMax0 = (yMax - ro.y) / rd.y;
    if(tYMin0 > tYMax0) { float tmp = tYMin0; tYMin0 = tYMax0; tYMax0 = tmp; }
    
    float tNear = max(t0, tYMin0);
    float tFar = min(t1, tYMax0);
    
    tNear = max(tNear, 0.0);
    
    if(tNear > tFar) return vec2(-1.0);
    return vec2(tNear, tFar);
}

void main() {
    // Build camera ray
    vec3 forward = normalize(iCamFront);
    vec3 right = normalize(cross(forward, iCamUp));
    vec3 up = cross(right, forward);
    
    // Ray direction from UV coordinates
    vec3 rd = normalize(forward + uv.x * iAspect * 0.5 * right + uv.y * 0.5 * up);
    vec3 ro = iCamPos;
    
    // Dark background
    vec3 bgColor = vec3(0.01, 0.01, 0.015);
    
    // Intersect with flame bounding volume
    vec2 tRange = intersectFlameVolume(ro, rd);
    
    if(tRange.x < 0.0) {
        fragColor = vec4(bgColor, 1.0);
        return;
    }
    
    // Raymarching parameters
    int maxSteps = 64;
    float stepSize = (tRange.y - tRange.x) / float(maxSteps);
    stepSize = max(stepSize, 0.01);
    
    // Accumulation
    vec3 accColor = vec3(0.0);
    float accAlpha = 0.0;
    
    float t = tRange.x;
    
    for(int i = 0; i < maxSteps; i++) {
        if(accAlpha > 0.95) break;  // Early exit when opaque enough
        
        vec3 p = ro + rd * t;
        
        // Sample flame density
        float density = flameDensity(p, iTime);
        
        if(density > 0.001) {
            // Get temperature at this point
            float height = clamp(p.y / 1.8, 0.0, 1.0);
            float radial = length(p.xz);
            float temp = getTemperature(p, density, iTime);
            
            // Get flame color from temperature
            vec3 col = flameColor(temp, height, radial);
            
            // Emission intensity (hot regions glow brighter)
            float emission = temp * temp * 3.0;
            col *= emission;
            
            // Beer-Lambert absorption
            float alpha = density * stepSize * 12.0;
            alpha = min(alpha, 0.3);  // Cap per-step contribution
            
            // Front-to-back compositing
            accColor += col * alpha * (1.0 - accAlpha);
            accAlpha += alpha * (1.0 - accAlpha);
        }
        
        t += stepSize;
        if(t > tRange.y) break;
    }
    
    // Add subtle glow around flame (scattering approximation)
    // Sample density at a few points near the center for glow
    vec3 glowCenter = vec3(0.0, 0.6, 0.0);
    float distToFlameAxis = length(ro + rd * max(tRange.x, 0.0) - glowCenter);
    float glow = exp(-distToFlameAxis * distToFlameAxis * 2.0) * 0.08 * iFormation;
    vec3 glowColor = vec3(1.0, 0.6, 0.15) * glow;
    
    // Final composite
    vec3 finalColor = bgColor * (1.0 - accAlpha) + accColor + glowColor;
    
    // Tone mapping (simple Reinhard)
    finalColor = finalColor / (finalColor + vec3(1.0));
    
    // Slight gamma correction
    finalColor = pow(finalColor, vec3(1.0 / 2.2));
    
    fragColor = vec4(finalColor, 1.0);
}
)";


/* =================== SHADER UTILITIES =================== */

GLuint makeProg(const char*vs,const char*fs){
    auto sh=[&](GLenum t,const char*s, const char* label){
        GLuint x=glCreateShader(t);
        glShaderSource(x,1,&s,0);
        glCompileShader(x);
        GLint ok=0;
        glGetShaderiv(x, GL_COMPILE_STATUS, &ok);
        if(!ok){
            char log[2048];
            glGetShaderInfoLog(x, 2048, nullptr, log);
            std::cerr << "[SHADER COMPILE ERROR] " << label << ":\n" << log << std::endl;
        }
        return x;
    };
    GLuint p=glCreateProgram();
    GLuint v=sh(GL_VERTEX_SHADER,vs,"vertex");
    GLuint f=sh(GL_FRAGMENT_SHADER,fs,"fragment");
    glAttachShader(p,v); glAttachShader(p,f);
    glLinkProgram(p);
    GLint linked=0;
    glGetProgramiv(p, GL_LINK_STATUS, &linked);
    if(!linked){
        char log[2048];
        glGetProgramInfoLog(p, 2048, nullptr, log);
        std::cerr << "[PROGRAM LINK ERROR]:\n" << log << std::endl;
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

/* =================== MAIN =================== */
int main(){
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow*w=glfwCreateWindow(1280,720,"Flame Simulation",0,0);
    if(!w) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(w);
    
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }
    
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "Starting volumetric flame simulation..." << std::endl;

    glfwSetCursorPosCallback(w,mouse);

    // Empty VAO for fullscreen triangle
    GLuint emptyVAO;
    glGenVertexArrays(1, &emptyVAO);

    // Build shader program
    GLuint flameProg = makeProg(fullscreenVS, flameFS);
    
    // Get uniform locations
    GLint uTime       = glGetUniformLocation(flameProg, "iTime");
    GLint uCamPos     = glGetUniformLocation(flameProg, "iCamPos");
    GLint uCamFront   = glGetUniformLocation(flameProg, "iCamFront");
    GLint uCamUp      = glGetUniformLocation(flameProg, "iCamUp");
    GLint uAspect     = glGetUniformLocation(flameProg, "iAspect");
    GLint uFormation  = glGetUniformLocation(flameProg, "iFormation");

    // Formation state
    float formationProgress = 0.0f;
    const float FORMATION_DURATION = 3.0f;
    bool formed = false;
    
    double last = glfwGetTime();
    float simTime = 0.0f;

    std::cout << "\n--- Controls ---" << std::endl;
    std::cout << "Right-click + drag: Orbit camera" << std::endl;
    std::cout << "ESC: Quit" << std::endl;
    std::cout << "----------------\n" << std::endl;
    std::cout << "Flame forming..." << std::endl;

    while(!glfwWindowShouldClose(w)){
        double now = glfwGetTime();
        float dt = (float)(now - last); 
        last = now;
        simTime += dt;

        // Handle input
        rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        glfwSetInputMode(w, GLFW_CURSOR, rmb ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        
        if(glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(w, true);

        // Formation progress
        if(!formed) {
            formationProgress += dt / FORMATION_DURATION;
            if(formationProgress >= 1.0f) {
                formationProgress = 1.0f;
                formed = true;
                std::cout << "\n========================================" << std::endl;
                std::cout << "Flame formation complete!" << std::endl;
                std::cout << "========================================\n" << std::endl;
            }
        }
        
        // Smooth ease-out for formation
        float easedFormation = 1.0f - powf(1.0f - formationProgress, 3.0f);

        // Get window size for aspect ratio
        int winW, winH;
        glfwGetFramebufferSize(w, &winW, &winH);
        float aspect = (float)winW / (float)winH;
        glViewport(0, 0, winW, winH);

        // Clear
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Render flame via fullscreen raymarching pass
        glUseProgram(flameProg);
        glUniform1f(uTime, simTime);
        glUniform3f(uCamPos, camPos.x, camPos.y, camPos.z);
        glUniform3f(uCamFront, camFront.x, camFront.y, camFront.z);
        glUniform3f(uCamUp, camUp.x, camUp.y, camUp.z);
        glUniform1f(uAspect, aspect);
        glUniform1f(uFormation, easedFormation);
        
        glBindVertexArray(emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(w);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &emptyVAO);
    glDeleteProgram(flameProg);
    glfwTerminate();
    return 0;
}
