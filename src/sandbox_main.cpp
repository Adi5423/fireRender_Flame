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

vec3 hash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

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

// ---- Flame shape ----
// Lighter / candle flame teardrop profile
// Reference: a pocket lighter flame is narrow at the gas nozzle,
// widens rapidly through the combustion zone reaching max width
// around 30-40% up, then tapers smoothly to a flickering tip.

float flameRadius(float h) {
    // h is normalized height [0..1] from base to tip
    // baseWidth controls the maximum width of the flame
    float baseWidth = 0.13;
    
    // 1. Quick rise from narrow nozzle at base
    //    Using 1-exp(-k*h) for fast initial widening
    float rise = 1.0 - exp(-h * 12.0);
    
    // 2. Smooth taper toward tip
    //    pow(1-h, p) controls how quickly it narrows
    float taper = pow(1.0 - h, 1.1);
    
    // 3. Slight bulge / widening in the combustion zone (h ~ 0.25-0.4)
    //    This is where the flame is widest
    float bulge = 1.0 + 0.3 * exp(-pow((h - 0.3) / 0.15, 2.0));
    
    return baseWidth * rise * taper * bulge;
}

float flameSDF(vec3 p) {
    float flameHeight = 2.0;
    float h = p.y / flameHeight;
    
    // Below base or above tip: outside
    if(h < 0.0 || h > 1.0) {
        // Return large positive value (outside)
        float radialDist = length(p.xz);
        return radialDist + abs(h) * 0.5;
    }
    
    float radius = flameRadius(h);
    float radialDist = length(p.xz);
    
    return radialDist - radius;
}

// Flame density with turbulence
float flameDensity(vec3 p, float time) {
    float flameHeight = 2.0;
    float h = p.y / flameHeight;
    
    // Quick reject outside vertical range
    if(h < -0.02 || h > 1.05) return 0.0;
    
    // --- Turbulence ---
    // Noise scrolls upward (simulating rising hot gas)
    vec3 noisePos = p;
    noisePos.y -= time * 1.8;
    
    // Turbulence amplitude increases with height
    // Base is stable, tip flickers wildly — this is physically correct:
    // the base is stabilized by the gas jet, the tip is in free convection
    float turbAmp = smoothstep(0.0, 0.5, h) * 0.8 + 0.1;
    
    // Large-scale sway (whole flame gently rocking)
    float sway = fbm(vec3(0.0, time * 0.4, 0.0), 2) * 0.03;
    
    // Medium turbulence (flame tongue motion)
    float turbX = fbm(noisePos * 3.0 + vec3(0, time * 0.5, 0), 3) * 0.08 * turbAmp;
    float turbZ = fbm(noisePos * 3.0 + vec3(50.0, time * 0.5, 30.0), 3) * 0.06 * turbAmp;
    
    // Fine flickering (high-frequency detail at the tip)
    float fineX = fbm(noisePos * 10.0 + vec3(0, time * 1.5, 0), 2) * 0.025 * turbAmp;
    float fineZ = fbm(noisePos * 10.0 + vec3(70.0, time * 1.5, 40.0), 2) * 0.02 * turbAmp;
    
    // Apply displacement
    vec3 displaced = p;
    displaced.x += sway + turbX + fineX;
    displaced.z += turbZ + fineZ;
    
    // Evaluate SDF
    float sdf = flameSDF(displaced);
    
    // Convert SDF to smooth density
    // Wider transition band for softer edges (more realistic)
    float density = 1.0 - smoothstep(-0.04, 0.03, sdf);
    
    // Internal density variation (the flame isn't uniform inside)
    float internalNoise = fbm(noisePos * 6.0 + vec3(0, time * 2.5, 0), 3);
    density *= 0.7 + 0.3 * (0.5 + 0.5 * internalNoise);
    
    // Smooth fade at the very base (flame emerges from nozzle)
    float baseFade = smoothstep(0.0, 0.06, h);
    density *= baseFade;
    
    // Dissolve at tip
    float tipFade = 1.0 - smoothstep(0.8, 1.0, h);
    density *= tipFade;
    
    // Formation animation
    density *= iFormation;
    
    return max(density, 0.0);
}

// ---- Temperature ----
// Temperature field determines both color and brightness
// Physically: hottest at base center (combustion zone), cools as gas rises

float getTemperature(vec3 p, float density, float time) {
    float flameHeight = 2.0;
    float h = clamp(p.y / flameHeight, 0.0, 1.0);
    float radial = length(p.xz);
    
    // Height-based cooling (convective heat loss as gas rises)
    // The bottom 30% stays very hot, then cools gradually
    float heightTemp = 1.0 - pow(h, 0.6);
    
    // Radial cooling (center axis is hottest, edges are coolest)
    float maxR = flameRadius(h) + 0.01;
    float radialTemp = 1.0 - smoothstep(0.0, maxR * 0.8, radial);
    
    // Combined temperature
    float temp = heightTemp * mix(0.35, 1.0, radialTemp);
    
    // Noise variation for flickering temperature
    vec3 noiseP = p;
    noiseP.y -= time * 1.5;
    float tempNoise = fbm(noiseP * 5.0, 2) * 0.12;
    temp += tempNoise;
    
    return clamp(temp * density, 0.0, 1.0);
}

// ---- Color mapping ----
// Maps temperature + height to realistic flame colors
// Reference: lighter flame from the image has:
//   - Blue-violet at very base (premixed combustion zone, CH radical emission)
//   - Bright white-yellow in inner core just above blue
//   - Rich orange on outer body
//   - Dark orange-red at outer edges and tip

vec3 flameColor(float temp, float h, float radial) {
    
    // --- Blue base zone ---
    // The blue zone is at the very bottom of the flame where
    // premixed combustion occurs (gas + air mix before burning)
    float blueZone = smoothstep(0.15, 0.0, h) * smoothstep(0.3, 0.6, temp);
    
    // Blue colors (from deep blue to light blue-white)
    vec3 deepBlue = vec3(0.05, 0.15, 0.7);
    vec3 lightBlue = vec3(0.3, 0.5, 0.95);
    vec3 blueCol = mix(deepBlue, lightBlue, temp);
    
    // --- Hot core zone ---
    // Just above blue zone, the inner core is white-hot
    // Bright yellow-white from incandescent soot particles
    vec3 whiteHot = vec3(1.0, 0.95, 0.85);
    vec3 brightYellow = vec3(1.0, 0.88, 0.45);
    
    // --- Main body colors ---
    // Orange zone (majority of visible flame)
    vec3 deepOrange = vec3(1.0, 0.45, 0.0);
    vec3 golden = vec3(1.0, 0.7, 0.15);
    
    // --- Outer / tip colors ---
    vec3 darkOrange = vec3(0.85, 0.25, 0.0);
    vec3 darkRed = vec3(0.5, 0.08, 0.0);
    vec3 smoke = vec3(0.15, 0.03, 0.0);
    
    // Build color based on temperature
    vec3 color;
    if(temp > 0.85) {
        // White-hot core
        color = mix(brightYellow, whiteHot, (temp - 0.85) / 0.15);
    } else if(temp > 0.65) {
        // Bright yellow
        color = mix(golden, brightYellow, (temp - 0.65) / 0.2);
    } else if(temp > 0.45) {
        // Golden to yellow
        color = mix(deepOrange, golden, (temp - 0.45) / 0.2);
    } else if(temp > 0.25) {
        // Orange
        color = mix(darkOrange, deepOrange, (temp - 0.25) / 0.2);
    } else if(temp > 0.1) {
        // Dark orange to dark red  
        color = mix(darkRed, darkOrange, (temp - 0.1) / 0.15);
    } else {
        // Smoke / nearly invisible
        color = mix(smoke, darkRed, temp / 0.1);
    }
    
    // Blend in blue at the base
    color = mix(color, blueCol, blueZone);
    
    return color;
}

// ---- Ray-Sphere intersection for bounding volume ----
vec2 intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if(disc < 0.0) return vec2(-1.0);
    float sqrtDisc = sqrt(disc);
    return vec2(-b - sqrtDisc, -b + sqrtDisc);
}

// ---- Main ----
void main() {
    // Build camera ray
    vec3 forward = normalize(iCamFront);
    vec3 right = normalize(cross(forward, iCamUp));
    vec3 up = cross(right, forward);
    
    vec3 rd = normalize(forward + uv.x * iAspect * 0.5 * right + uv.y * 0.5 * up);
    vec3 ro = iCamPos;
    
    // Pure black background
    vec3 bgColor = vec3(0.005, 0.005, 0.008);
    
    // Bounding sphere centered on the flame body
    // Flame extends from y=0 to y=2.0, center ~ (0, 1.0, 0), radius ~ 1.2
    vec3 sphereCenter = vec3(0.0, 1.0, 0.0);
    float sphereRadius = 1.3;
    vec2 tRange = intersectSphere(ro, rd, sphereCenter, sphereRadius);
    
    // Compute glow for ALL pixels (prevents visible bounding volume edge)
    // Project flame center onto ray to find closest approach
    vec3 toCenter = sphereCenter - ro;
    float tClosest = dot(toCenter, rd);
    vec3 closestPoint = ro + rd * max(tClosest, 0.0);
    float distToAxis = length(closestPoint.xz);  // distance to flame central axis
    float distToCenter = length(closestPoint - sphereCenter);
    
    // Warm ambient glow: soft halo around flame visible from distance
    float glowFalloff = exp(-distToCenter * distToCenter * 1.5);
    float axisGlow = exp(-distToAxis * distToAxis * 8.0);
    float glow = (glowFalloff * 0.04 + axisGlow * 0.02) * iFormation;
    vec3 warmGlow = vec3(1.0, 0.5, 0.1) * glow;
    
    // If ray misses bounding volume, just show background + glow
    if(tRange.x < 0.0) {
        vec3 finalColor = bgColor + warmGlow;
        finalColor = finalColor / (finalColor + vec3(1.0));
        finalColor = pow(finalColor, vec3(1.0 / 2.2));
        fragColor = vec4(finalColor, 1.0);
        return;
    }
    
    // Clamp to positive (in front of camera)
    tRange.x = max(tRange.x, 0.0);
    
    // Raymarching
    int maxSteps = 80;
    float totalDist = tRange.y - tRange.x;
    float stepSize = totalDist / float(maxSteps);
    stepSize = max(stepSize, 0.008);
    
    vec3 accColor = vec3(0.0);
    float accAlpha = 0.0;
    float t = tRange.x;
    
    for(int i = 0; i < maxSteps; i++) {
        if(accAlpha > 0.97) break;
        
        vec3 p = ro + rd * t;
        
        float density = flameDensity(p, iTime);
        
        if(density > 0.001) {
            float h = clamp(p.y / 2.0, 0.0, 1.0);
            float radial = length(p.xz);
            float temp = getTemperature(p, density, iTime);
            
            vec3 col = flameColor(temp, h, radial);
            
            // Emission: hotter regions emit more light
            // Using a curve that makes the core really bright
            float emission = pow(temp, 1.5) * 3.5;
            col *= emission;
            
            // Absorption / opacity per step
            float alpha = density * stepSize * 15.0;
            alpha = min(alpha, 0.25);
            
            // Front-to-back compositing
            accColor += col * alpha * (1.0 - accAlpha);
            accAlpha += alpha * (1.0 - accAlpha);
        }
        
        t += stepSize;
        if(t > tRange.y) break;
    }
    
    // Final composite: background + flame + glow
    vec3 finalColor = bgColor * (1.0 - accAlpha) + accColor + warmGlow;
    
    // Tone mapping (Reinhard)
    finalColor = finalColor / (finalColor + vec3(1.0));
    
    // Gamma correction
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
