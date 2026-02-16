#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <cstddef>

/* =================== MATH =================== */
#include "math_utils.h"

/* =================== CAMERA =================== */
Vec3 camPos = {0.0f, 0.8f, 3.0f};
Vec3 camFront = {0, 0, -1};
Vec3 camUp = {0, 1, 0};
float yaw = -90.0f, pitch = -5.0f;
bool rmb = false, firstMouse = true;
float lastX = 640, lastY = 360;

// Camera speed constants
constexpr float CAM_SPEED = 2.5f;
constexpr float CAM_SPEED_FAST = 7.0f;  // When holding Shift

void mouse(GLFWwindow*, double x, double y) {
    if (!rmb) { firstMouse = true; return; }
    if (firstMouse) { lastX = (float)x; lastY = (float)y; firstMouse = false; }

    float dx = (float)x - lastX, dy = lastY - (float)y;
    lastX = (float)x; lastY = (float)y;

    dx *= 0.1f; dy *= 0.1f;
    yaw += dx; pitch += dy;
    pitch = fmaxf(-89, fminf(89, pitch));

    camFront = normalize({
        cosf(yaw * 0.0174533f) * cosf(pitch * 0.0174533f),
        sinf(pitch * 0.0174533f),
        sinf(yaw * 0.0174533f) * cosf(pitch * 0.0174533f)
    });
}

// Process WASD + Q/E movement (only when RMB held)
void processMovement(GLFWwindow* w, float dt) {
    if (!rmb) return;

    bool shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                 glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    float speed = (shift ? CAM_SPEED_FAST : CAM_SPEED) * dt;

    Vec3 front = normalize(camFront);
    Vec3 right = normalize(cross(front, camUp));

    if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS)
        camPos = camPos + front * speed;
    if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS)
        camPos = camPos + front * (-speed);
    if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS)
        camPos = camPos + right * (-speed);
    if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS)
        camPos = camPos + right * speed;
    if (glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS)
        camPos.y -= speed;
    if (glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS)
        camPos.y += speed;
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
uniform float iFormation;

// =============================================
// NOISE — Optimized GPU noise functions
// =============================================

// Fast hash (no sin — avoids GPU precision issues on some hardware)
vec3 hash33(vec3 p) {
    uvec3 q = uvec3(ivec3(p)) * uvec3(1597334673u, 3812015801u, 2798796415u);
    q = (q.x ^ q.y ^ q.z) * uvec3(1597334673u, 3812015801u, 2798796415u);
    return -1.0 + 2.0 * vec3(q) * (1.0 / float(0xffffffffu));
}

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    // Quintic Hermite for smoother interpolation (less grid artifacts)
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    return mix(mix(mix(dot(hash33(i + vec3(0,0,0)), f - vec3(0,0,0)),
                       dot(hash33(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x),
                   mix(dot(hash33(i + vec3(0,1,0)), f - vec3(0,1,0)),
                       dot(hash33(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
               mix(mix(dot(hash33(i + vec3(0,0,1)), f - vec3(0,0,1)),
                       dot(hash33(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x),
                   mix(dot(hash33(i + vec3(0,1,1)), f - vec3(0,1,1)),
                       dot(hash33(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z);
}

// FBM with rotation between octaves to break grid alignment
float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amp = 0.5;
    // Rotation matrix to decorrelate octaves (reduces visible banding)
    mat3 rot = mat3(0.00, 0.80, 0.60,
                   -0.80, 0.36,-0.48,
                   -0.60,-0.48, 0.64);
    for(int i = 0; i < octaves; i++) {
        value += amp * noise3D(p);
        p = rot * p * 2.0 + vec3(1.7, 9.2, 3.1);
        amp *= 0.5;
    }
    return value;
}

// =============================================
// FLAME SHAPE — Lighter flame teardrop profile
// =============================================
// Reference: real lighter flame
//   - Narrow at nozzle (base)
//   - Widens through combustion zone (widest ~35% up)
//   - Smooth, slightly elongated taper to tip
//   - Overall aspect ratio ~3:1 (tall and slender)

const float FLAME_HEIGHT = 2.2;
const float FLAME_BASE_WIDTH = 0.12;

float flameRadius(float h) {
    // h in [0..1]: 0=base, 1=tip
    
    // Fast rise from narrow nozzle point
    float rise = 1.0 - exp(-h * 15.0);
    
    // Smooth taper toward tip
    // pow < 1.0 makes the top part rounder, > 1.0 makes it pointier
    float taper = pow(max(1.0 - h, 0.0), 1.2);
    
    // Bell-shaped combustion zone bulge, peaking at h=0.35
    float bulge = 1.0 + 0.35 * exp(-pow((h - 0.35) / 0.18, 2.0));
    
    return FLAME_BASE_WIDTH * rise * taper * bulge;
}

float flameSDF(vec3 p) {
    float h = p.y / FLAME_HEIGHT;
    
    if(h < -0.01 || h > 1.01) {
        return length(p.xz) + abs(p.y) * 0.3 + 0.1;
    }
    
    float hc = clamp(h, 0.0, 1.0);
    float radius = flameRadius(hc);
    float radialDist = length(p.xz);
    
    return radialDist - radius;
}

// =============================================
// DENSITY — Flame density with turbulence
// =============================================

float flameDensity(vec3 p, float time) {
    float h = p.y / FLAME_HEIGHT;
    
    // Quick reject
    if(h < -0.01 || h > 1.05) return 0.0;
    
    // --- Upward-scrolling noise coordinates ---
    vec3 noisePos = p;
    noisePos.y -= time * 2.0;  // rising motion
    
    // Turbulence strongest at tip, weakest at base
    // This is physically correct: the fuel jet stabilizes the base,
    // while the tip is subject to free convective instability
    float turbHeight = smoothstep(0.05, 0.6, h);
    float turbAmp = 0.08 + turbHeight * 0.18;
    
    // Gentle whole-flame sway (very low frequency)
    float swayX = noise3D(vec3(time * 0.3, 0.0, 0.0)) * 0.015;
    float swayZ = noise3D(vec3(0.0, 0.0, time * 0.25)) * 0.012;
    
    // Medium turbulence (flame tongue motion)
    float turbX = fbm(noisePos * 3.5, 3) * turbAmp;
    float turbZ = fbm(noisePos * 3.5 + vec3(43.0, 17.0, 31.0), 3) * turbAmp * 0.8;
    
    // Fine flickering at tip
    float fineAmp = turbHeight * 0.04;
    float fineX = fbm(noisePos * 9.0 + vec3(0, time * 1.2, 0), 2) * fineAmp;
    float fineZ = fbm(noisePos * 9.0 + vec3(67.0, time * 1.2, 41.0), 2) * fineAmp * 0.7;
    
    // Displaced sample point
    vec3 dp = p;
    dp.x += swayX + turbX + fineX;
    dp.z += swayZ + turbZ + fineZ;
    
    // Evaluate SDF at displaced position
    float sdf = flameSDF(dp);
    
    // SDF → density with smooth, wide falloff for soft edges
    float density = 1.0 - smoothstep(-0.05, 0.035, sdf);
    
    // Internal density variation (flame isn't solid)
    float intNoise = fbm(noisePos * 5.0 + vec3(0, time * 2.0, 0), 2);
    density *= 0.65 + 0.35 * (0.5 + 0.5 * intNoise);
    
    // Base fade (flame emerges from a point source)
    density *= smoothstep(0.0, 0.05, h);
    
    // Tip dissolve
    density *= 1.0 - smoothstep(0.75, 1.0, h);
    
    // Formation scale
    density *= iFormation;
    
    return max(density, 0.0);
}

// =============================================
// TEMPERATURE — Physically based temperature field
// =============================================

float getTemperature(vec3 p, float density, float time) {
    float h = clamp(p.y / FLAME_HEIGHT, 0.0, 1.0);
    float radial = length(p.xz);
    float maxR = flameRadius(h) + 0.01;
    
    // Convective cooling with height
    // Bottom ~30% stays very hot, then exponential decline
    float heightTemp = exp(-h * 1.8) * 0.7 + (1.0 - h) * 0.3;
    
    // Radial: hottest on center axis, coolest at edges
    float radialFactor = 1.0 - smoothstep(0.0, maxR * 0.85, radial);
    
    // Combined: core is hot, edges are cool
    float temp = heightTemp * mix(0.3, 1.0, radialFactor);
    
    // Slight noise flicker in temperature
    vec3 nP = p;
    nP.y -= time * 1.6;
    temp += fbm(nP * 4.0, 2) * 0.1;
    
    return clamp(temp * density, 0.0, 1.0);
}

// Reference image analysis:
//   Blue-violet zone at very base (premixed CH combustion)
//   Transition: blue fades into bright inner core
//   Inner core: intense yellow-white (incandescent soot)
//   Outer body: rich orange (cooler soot)
//   Outer edges: dark orange -> dark red -> transparent
//   Tip: orange-red, dissolving into darkness

// Helper: radial factor (0 at edge, 1 at center axis)
float radialFactor(float radial, float h) {
    float maxR = flameRadius(h) + 0.01;
    return 1.0 - smoothstep(0.0, maxR, radial);
}

vec3 flameColor(float temp, float h, float radial) {
    // --- Temperature-based color bands ---
    vec3 color;
    
    // White-hot core (T > 0.82)
    vec3 whiteHot    = vec3(1.0, 0.96, 0.88);
    vec3 brightYellow = vec3(1.0, 0.9, 0.5);
    
    // Mid flame
    vec3 golden      = vec3(1.0, 0.72, 0.18);
    vec3 deepOrange  = vec3(1.0, 0.48, 0.02);
    
    // Cool outer edges
    vec3 darkOrange  = vec3(0.88, 0.28, 0.0);
    vec3 darkRed     = vec3(0.55, 0.1, 0.0);
    vec3 dimSmoke    = vec3(0.18, 0.04, 0.0);
    
    if(temp > 0.82) {
        color = mix(brightYellow, whiteHot, (temp - 0.82) / 0.18);
    } else if(temp > 0.62) {
        color = mix(golden, brightYellow, (temp - 0.62) / 0.2);
    } else if(temp > 0.42) {
        color = mix(deepOrange, golden, (temp - 0.42) / 0.2);
    } else if(temp > 0.24) {
        color = mix(darkOrange, deepOrange, (temp - 0.24) / 0.18);
    } else if(temp > 0.1) {
        color = mix(darkRed, darkOrange, (temp - 0.1) / 0.14);
    } else {
        color = mix(dimSmoke, darkRed, temp / 0.1);
    }
    
    // --- Blue base zone ---
    // In a real lighter flame, the bottom ~15-20% has a prominent blue cone
    // from premixed combustion (CH radical emission at ~430nm)
    // The blue is INDEPENDENT of temperature — it's chemiluminescence
    
    // Blue zone strength: strong at base, fades out by h=0.22
    float blueHeight = smoothstep(0.22, 0.02, h);
    float blueRadial = 1.0 - smoothstep(0.0, flameRadius(h) * 1.2, radial);
    float blueStrength = blueHeight * blueRadial;
    
    // Blue colors matching reference image
    vec3 innerBlue = vec3(0.25, 0.45, 1.0);
    vec3 outerBlue = vec3(0.08, 0.2, 0.7);
    float rFac = radialFactor(radial, h);
    vec3 blueCol = mix(outerBlue, innerBlue, rFac);
    
    // Blend blue clearly into the base region
    color = mix(color, blueCol, blueStrength * 0.75);
    
    return color;
}

// =============================================
// RAY INTERSECTION
// =============================================

vec2 intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if(disc < 0.0) return vec2(-1.0);
    float s = sqrt(disc);
    return vec2(-b - s, -b + s);
}

// =============================================
// MAIN — Raymarching with adaptive stepping
// =============================================

void main() {
    // Build camera ray
    vec3 forward = normalize(iCamFront);
    vec3 right = normalize(cross(forward, iCamUp));
    vec3 up = cross(right, forward);
    
    vec3 rd = normalize(forward + uv.x * iAspect * 0.5 * right + uv.y * 0.5 * up);
    vec3 ro = iCamPos;
    
    // Pure black background
    vec3 bgColor = vec3(0.003, 0.003, 0.006);
    
    // Bounding sphere
    vec3 sphereCenter = vec3(0.0, FLAME_HEIGHT * 0.45, 0.0);
    float sphereRadius = FLAME_HEIGHT * 0.65;
    vec2 tRange = intersectSphere(ro, rd, sphereCenter, sphereRadius);
    
    // Glow for ALL pixels (ambient warm light cast by flame)
    vec3 flameCenter = vec3(0.0, FLAME_HEIGHT * 0.35, 0.0);
    vec3 toC = flameCenter - ro;
    float tProj = max(dot(toC, rd), 0.0);
    vec3 closest = ro + rd * tProj;
    float dAxis = length(closest.xz);
    float dCenter = length(closest - flameCenter);
    
    float glowAmt = exp(-dCenter * dCenter * 1.2) * 0.035
                  + exp(-dAxis * dAxis * 10.0) * 0.015;
    glowAmt *= iFormation;
    vec3 warmGlow = vec3(1.0, 0.5, 0.12) * glowAmt;
    
    if(tRange.x < 0.0) {
        // Miss — background + glow only
        vec3 c = bgColor + warmGlow;
        c = c / (c + 1.0);
        c = pow(c, vec3(1.0/2.2));
        fragColor = vec4(c, 1.0);
        return;
    }
    
    tRange.x = max(tRange.x, 0.0);
    
    // --- Adaptive-step raymarching ---
    // Fewer steps in empty regions, more steps inside the flame
    float totalDist = tRange.y - tRange.x;
    float baseStep = totalDist / 64.0;
    baseStep = max(baseStep, 0.01);
    
    vec3 accColor = vec3(0.0);
    float accAlpha = 0.0;
    float t = tRange.x;
    int steps = 0;
    const int MAX_STEPS = 96;
    
    for(int i = 0; i < MAX_STEPS; i++) {
        if(accAlpha > 0.97 || t > tRange.y) break;
        
        vec3 p = ro + rd * t;
        float density = flameDensity(p, iTime);
        
        if(density > 0.001) {
            float h = clamp(p.y / FLAME_HEIGHT, 0.0, 1.0);
            float radial = length(p.xz);
            float temp = getTemperature(p, density, iTime);
            vec3 col = flameColor(temp, h, radial);
            
            // Emission: pow curve makes core dramatically brighter
            float emission = pow(temp, 1.6) * 3.5;
            col *= emission;
            
            // Opacity per step (Beer-Lambert)
            float stepLen = baseStep * 0.6;  // finer steps inside flame
            float alpha = density * stepLen * 18.0;
            alpha = min(alpha, 0.2);
            
            accColor += col * alpha * (1.0 - accAlpha);
            accAlpha += alpha * (1.0 - accAlpha);
            
            t += stepLen;
        } else {
            // Empty space — take a larger step
            t += baseStep * 1.4;
        }
    }
    
    // Final composite
    vec3 finalColor = bgColor * (1.0 - accAlpha) + accColor + warmGlow;
    
    // Filmic tone mapping (slightly more contrast than Reinhard)
    finalColor = finalColor / (finalColor + 0.8) * 1.1;
    
    // Gamma
    finalColor = pow(max(finalColor, vec3(0.0)), vec3(1.0/2.2));
    
    fragColor = vec4(finalColor, 1.0);
}
)";


/* =================== SHADER UTILITIES =================== */

GLuint makeProg(const char* vs, const char* fs) {
    auto sh = [&](GLenum t, const char* s, const char* label) {
        GLuint x = glCreateShader(t);
        glShaderSource(x, 1, &s, 0);
        glCompileShader(x);
        GLint ok = 0;
        glGetShaderiv(x, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(x, 2048, nullptr, log);
            std::cerr << "[SHADER COMPILE ERROR] " << label << ":\n" << log << std::endl;
        }
        return x;
    };
    GLuint p = glCreateProgram();
    GLuint v = sh(GL_VERTEX_SHADER, vs, "vertex");
    GLuint f = sh(GL_FRAGMENT_SHADER, fs, "fragment");
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint linked = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048];
        glGetProgramInfoLog(p, 2048, nullptr, log);
        std::cerr << "[PROGRAM LINK ERROR]:\n" << log << std::endl;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

/* =================== MAIN =================== */
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* w = glfwCreateWindow(1280, 720, "Flame Simulation", 0, 0);
    if (!w) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(w);
    glfwSwapInterval(1);  // VSync for smooth rendering

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "Starting volumetric flame simulation..." << std::endl;

    glfwSetCursorPosCallback(w, mouse);

    // Empty VAO for fullscreen triangle
    GLuint emptyVAO;
    glGenVertexArrays(1, &emptyVAO);

    // Build shader program
    GLuint flameProg = makeProg(fullscreenVS, flameFS);

    // Get uniform locations
    GLint uTime = glGetUniformLocation(flameProg, "iTime");
    GLint uCamPos = glGetUniformLocation(flameProg, "iCamPos");
    GLint uCamFront = glGetUniformLocation(flameProg, "iCamFront");
    GLint uCamUp = glGetUniformLocation(flameProg, "iCamUp");
    GLint uAspect = glGetUniformLocation(flameProg, "iAspect");
    GLint uFormation = glGetUniformLocation(flameProg, "iFormation");

    // Formation state
    float formationProgress = 0.0f;
    const float FORMATION_DURATION = 2.5f;
    bool formed = false;

    double last = glfwGetTime();
    float simTime = 0.0f;

    // FPS tracking
    double fpsTimer = 0.0;
    int frameCount = 0;

    std::cout << "\n--- Controls ---" << std::endl;
    std::cout << "Hold RMB + Mouse:    Look around" << std::endl;
    std::cout << "Hold RMB + W/A/S/D:  Move forward/left/back/right" << std::endl;
    std::cout << "Hold RMB + Q/E:      Move down/up" << std::endl;
    std::cout << "Hold RMB + Shift:    Move faster" << std::endl;
    std::cout << "ESC:                 Quit" << std::endl;
    std::cout << "----------------\n" << std::endl;
    std::cout << "Flame forming..." << std::endl;

    while (!glfwWindowShouldClose(w)) {
        double now = glfwGetTime();
        float dt = (float)(now - last);
        last = now;
        dt = fminf(dt, 0.05f);  // Cap delta time to prevent physics explosion on lag spike
        simTime += dt;

        // FPS counter
        fpsTimer += dt;
        frameCount++;
        if (fpsTimer >= 2.0) {
            float fps = frameCount / (float)fpsTimer;
            char title[64];
            snprintf(title, sizeof(title), "Flame Simulation | %.1f FPS", fps);
            glfwSetWindowTitle(w, title);
            fpsTimer = 0.0;
            frameCount = 0;
        }

        // Input
        rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        glfwSetInputMode(w, GLFW_CURSOR, rmb ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        if (glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(w, true);

        // Camera movement (only when RMB held)
        processMovement(w, dt);

        // Formation
        if (!formed) {
            formationProgress += dt / FORMATION_DURATION;
            if (formationProgress >= 1.0f) {
                formationProgress = 1.0f;
                formed = true;
                std::cout << "\n========================================" << std::endl;
                std::cout << "Flame formation complete!" << std::endl;
                std::cout << "========================================\n" << std::endl;
            }
        }

        float easedFormation = 1.0f - powf(1.0f - formationProgress, 3.0f);

        // Viewport
        int winW, winH;
        glfwGetFramebufferSize(w, &winW, &winH);
        float aspect = (float)winW / (float)winH;
        glViewport(0, 0, winW, winH);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Render
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
