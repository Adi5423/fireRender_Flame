#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <cstddef>

/* =================== MATH =================== */
#include "math_utils.h"
#include "noise.h"

/* =================== CAMERA =================== */
Vec3 camPos={0,0,6}, camFront={0,0,-1}, camUp={0,1,0};
float yaw=-90,pitch=0;
bool rmb=false, firstMouse=true;
float lastX=640,lastY=360;

void mouse(GLFWwindow*,double x,double y){
    if(!rmb){ firstMouse=true; return; }
    if(firstMouse){ lastX=x; lastY=y; firstMouse=false; }

    float dx=x-lastX, dy=lastY-y;
    lastX=x; lastY=y;

    dx*=0.1f; dy*=0.1f;
    yaw+=dx; pitch+=dy;
    pitch = fmaxf(-89,fminf(89,pitch));

    camFront = normalize({
        cosf(yaw*0.0174533f)*cosf(pitch*0.0174533f),
        sinf(pitch*0.0174533f),
        sinf(yaw*0.0174533f)*cosf(pitch*0.0174533f)
    });
}

/* =================== MATRICES =================== */
void perspective(float*m,float f,float a,float n,float fr){
    float t=tanf(f/2);
    for(int i=0;i<16;i++) m[i]=0;
    m[0]=1/(a*t); m[5]=1/t;
    m[10]=-(fr+n)/(fr-n); m[11]=-1;
    m[14]=-(2*fr*n)/(fr-n);
}

void lookAt(float*m){
    Vec3 f=normalize(camFront);
    Vec3 r=normalize(cross(f,camUp));
    Vec3 u=cross(r,f);

    m[0]=r.x; m[4]=r.y; m[8]=r.z;  m[12]=-(r.x*camPos.x+r.y*camPos.y+r.z*camPos.z);
    m[1]=u.x; m[5]=u.y; m[9]=u.z;  m[13]=-(u.x*camPos.x+u.y*camPos.y+u.z*camPos.z);
    m[2]=-f.x;m[6]=-f.y;m[10]=-f.z;m[14]=(f.x*camPos.x+f.y*camPos.y+f.z*camPos.z);
    m[15]=1;
}

void rotateY(float*m,float a){
    for(int i=0;i<16;i++) m[i]=0;
    m[0]=cosf(a); m[8]=sinf(a);
    m[2]=-sinf(a);m[10]=cosf(a);
    m[5]=1; m[15]=1;
}

void mul(float*o,float*a,float*b){
    for(int c=0;c<4;c++)
        for(int r=0;r<4;r++){
            o[c*4+r]=0;
            for(int k=0;k<4;k++)
                o[c*4+r]+=a[k*4+r]*b[c*4+k];
        }
}

/* =================== PARTICLES =================== */
struct Particle {
    Vec3 pos;
    Vec3 vel;
    float temperature;  // 0.0 to 1.0 (1.0 = hottest)
    float age;          // Time since spawn
    float maxLife;      // Randomized lifetime
};

constexpr int MAX_PARTS = 256;
Particle parts[MAX_PARTS];

// Draw data struct (pos + temp)
struct DrawPart { Vec3 p; float t; };

/* =================== PHYSICS CONSTANTS =================== */
// Gravity: Standard Earth gravity (points down)
constexpr float GRAVITY = -9.8f;

// Thermal Buoyancy: Coefficient scaling the upward force due to temperature
// Based on Boussinesq approximation: F_buoyancy = Beta * g * (T - T_ambient)
constexpr float THERMAL_BUOYANCY = 15.0f;  

// Ambient Temperature: Temperature of surrounding air (0.0 for relative scale)
constexpr float AMBIENT_TEMP = 0.0f;

// Spawn Temperature: Initial temperature of burning fuel (normalized 0.0-1.0)
constexpr float SPAWN_TEMP = 1.0f;

// Cooling Rate: Exponential decay factor for temperature loss
// Larger value = faster cooling = shorter flame
constexpr float COOLING_RATE = 0.8f;

// Flame Base Y: Vertical position where combustion starts
constexpr float FLAME_BASE_Y = -0.5f;

// Spawn Radius: size of the burner/wick area
constexpr float SPAWN_RADIUS = 0.15f;

/* =================== FLAME SHAPE =================== */
// Enforces the cone shape of the flame
// Real flames tapers because:
// 1. Air is entrained from sides, cooling the outer edge
// 2. The hot core rises fastest, creating a central column
void applyFlameShape(Particle& p, float dt, float flameBaseY, float spawnRadius){
    float height = p.pos.y - flameBaseY;
    if(height < 0) return;  // Below base, no constraint
    
    // Cone taper: radius decreases linearly with height
    // Max radius shrinks as we go up
    float maxRadius = spawnRadius * (1.0f - height / 3.0f);
    maxRadius = fmaxf(maxRadius, 0.05f);  // Minimum radius at tip
    
    float radialDist = length2D(p.pos.x, p.pos.z);
    
    // If particle is outside the cone, gently push it back in
    if(radialDist > maxRadius && radialDist > 0.001f){
        // Soft constraint - push toward center axis
        float excess = radialDist - maxRadius;
        float pushStrength = excess * 3.0f;  // Spring constant
        
        // Radial direction (normalized)
        float invDist = 1.0f / radialDist;
        float dirX = p.pos.x * invDist;
        float dirZ = p.pos.z * invDist;
        
        // Apply inward force (acceleration modification)
        p.vel.x -= dirX * pushStrength * dt;
        p.vel.z -= dirZ * pushStrength * dt;
    }
}

/* =================== SHADERS =================== */

// ---- Mesh ----
const char* meshVS = R"(
#version 460 core
layout(location=0) in vec3 p;
uniform mat4 MVP;
void main(){ gl_Position=MVP*vec4(p,1); }
)";

const char* meshFS = R"(
#version 460 core
out vec4 c;
void main(){ c=vec4(1.0,0.6,0.2,1.0); }
)";

// ---- Background ----
const char* bgVS = R"(
#version 460 core
const vec2 v[3]=vec2[]( vec2(-1,-1), vec2(3,-1), vec2(-1,3) );
out vec2 uv;
void main(){
    gl_Position=vec4(v[gl_VertexID],0,1);
    uv=gl_Position.xy*0.5+0.5;
}
)";

const char* bgFS = R"(
#version 460 core
in vec2 uv;
out vec4 c;
void main(){
    vec3 top=vec3(0.55,0.75,0.95);
    vec3 bot=vec3(0.0);
    c=vec4(mix(bot,top,uv.y),1);
}
)";

// ---- Particles ----
const char* partVS = R"(
#version 460 core
layout(location=0) in vec3 p;
layout(location=1) in float temp;
uniform mat4 MVP;
uniform float size;
out float vTemp;
void main(){
    gl_Position = MVP * vec4(p,1);
    gl_PointSize = size * (0.8 + temp * 0.4);
    vTemp = temp;
}
)";

const char* partFS = R"(
#version 460 core
in float vTemp;
out vec4 c;

vec3 temperatureColor(float t) {
    // Blackbody approximation
    if (t > 0.8) return mix(vec3(1.0, 0.9, 0.6), vec3(1.0, 1.0, 1.0), (t-0.8)*5.0);  // Yellow -> White
    if (t > 0.5) return mix(vec3(1.0, 0.4, 0.1), vec3(1.0, 0.9, 0.6), (t-0.5)*3.33);  // Orange -> Yellow
    if (t > 0.2) return mix(vec3(0.5, 0.05, 0.0), vec3(1.0, 0.4, 0.1), (t-0.2)*3.33);  // Dark Red -> Orange
    return mix(vec3(0.1, 0.1, 0.1), vec3(0.5, 0.05, 0.0), t*5.0);  // Smoke -> Dark Red
}

void main(){
    float d = length(gl_PointCoord - vec2(0.5));
    if(d > 0.5) discard;
    
    vec3 color = temperatureColor(vTemp);
    float alpha = (1.0 - d*2.0);
    // Fade out as it cools (smoke is transparent)
    alpha *= (vTemp < 0.2) ? vTemp*3.0 : 1.0;
    
    c = vec4(color, alpha);
}
)";

GLuint makeProg(const char*vs,const char*fs){
    auto sh=[&](GLenum t,const char*s){
        GLuint x=glCreateShader(t);
        glShaderSource(x,1,&s,0);
        glCompileShader(x);
        return x;
    };
    GLuint p=glCreateProgram();
    GLuint v=sh(GL_VERTEX_SHADER,vs);
    GLuint f=sh(GL_FRAGMENT_SHADER,fs);
    glAttachShader(p,v); glAttachShader(p,f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

/* =================== MAIN =================== */
int main(){
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow*w=glfwCreateWindow(1280,720,"Flame Buoyancy Demo",0,0);
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
    std::cout << "Starting simulation..." << std::endl;

    glfwSetCursorPosCallback(w,mouse);
    glEnable(GL_DEPTH_TEST);

    // ---- Mesh ----
    float mesh[]{
        -0.7f,-0.5f,0.4f, 0.7f,-0.5f,0.4f, 0,0.8f,0.4f,
        -0.7f,-0.5f,-0.4f, 0,0.8f,-0.4f, 0.7f,-0.5f,-0.4f
    };

    GLuint VAO,VBO;
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1,&VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(mesh),mesh,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,0,0,0);
    glEnableVertexAttribArray(0);

    // ---- Particle init ----
    for(auto& p:parts){
        // Spawn in circular pattern at flame base
        float angle = ((rand()%100)/100.f) * 6.28318f;  // Random angle
        float radius = ((rand()%100)/100.f) * SPAWN_RADIUS;
        
        p.pos = {
            cosf(angle) * radius,
            FLAME_BASE_Y,
            sinf(angle) * radius
        };
        
        p.vel = {
            ((rand()%100)/100.f-0.5f)*0.2f,  // Small radial spread
            0.5f+(rand()%100)/200.f,          // Small initial upward
            ((rand()%100)/100.f-0.5f)*0.2f
        };
        
        p.temperature = SPAWN_TEMP;
        p.age = 0.0f;
        p.maxLife = 0.8f + ((rand()%100)/100.f) * 0.7f;  // 0.8 to 1.5 seconds
    }

    GLuint pVAO,pVBO;
    glGenVertexArrays(1,&pVAO);
    glGenBuffers(1,&pVBO);
    glBindVertexArray(pVAO);
    glBindBuffer(GL_ARRAY_BUFFER,pVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(DrawPart)*MAX_PARTS,nullptr,GL_DYNAMIC_DRAW);
    
    // Attrib 0: Pos
    glVertexAttribPointer(0,3,GL_FLOAT,0,sizeof(DrawPart),(void*)0);
    glEnableVertexAttribArray(0);
    // Attrib 1: Temp
    glVertexAttribPointer(1,1,GL_FLOAT,0,sizeof(DrawPart),(void*)offsetof(DrawPart, t));
    glEnableVertexAttribArray(1);

    GLuint meshProg=makeProg(meshVS,meshFS);
    GLuint bgProg=makeProg(bgVS,bgFS);
    GLuint partProg=makeProg(partVS,partFS);

    GLint mvpLoc=glGetUniformLocation(meshProg,"MVP");
    GLint pMVP=glGetUniformLocation(partProg,"MVP");
    GLint pSize=glGetUniformLocation(partProg,"size");

    float P[16],V[16],M[16],PV[16],MVP[16];
    double last=glfwGetTime();

    while(!glfwWindowShouldClose(w)){
        double now=glfwGetTime();
        float dt=now-last; last=now;

        rmb=glfwGetMouseButton(w,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        glfwSetInputMode(w,GLFW_CURSOR,rmb?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);

        // ---- Update particles (temperature-based buoyancy) ----
        for(auto& p:parts){
            // Temperature decay (exponential cooling)
            p.temperature *= expf(-COOLING_RATE * dt);
            
            // Buoyancy force based on temperature (Boussinesq approximation)
            float buoyancy = THERMAL_BUOYANCY * (p.temperature - AMBIENT_TEMP);
            p.vel.y += buoyancy * dt;
            
            // Weak gravity on cooled particles (smoke behavior)
            if(p.temperature < 0.3f){
                p.vel.y += GRAVITY * dt * 0.1f;
            }
            
            // Turbulence (curl noise)
            // Scale turbulence by temperature (hotter = more turbulent)
            Vec3 turbulence = curlNoise(p.pos * 2.0f, (float)now * 1.5f) * (0.5f + p.temperature);
            p.vel = p.vel + turbulence * dt * 2.0f;  // Apply force
            
            // Flame shape constraint (cone geometry)
            applyFlameShape(p, dt, FLAME_BASE_Y, SPAWN_RADIUS);
            
            // Position integration
            p.pos = p.pos + p.vel * dt;
            
            // Age tracking
            p.age += dt;
            
            // Respawn logic
            if(p.age > p.maxLife || p.pos.y > 3.0f){
                // Respawn at base with random radial offset
                float angle = ((rand()%100)/100.f) * 6.28318f;
                float radius = ((rand()%100)/100.f) * SPAWN_RADIUS;
                
                p.pos = {
                    cosf(angle) * radius,
                    FLAME_BASE_Y,
                    sinf(angle) * radius
                };
                
                p.vel = {
                    ((rand()%100)/100.f-0.5f)*0.2f,
                    0.5f+(rand()%100)/200.f,
                    ((rand()%100)/100.f-0.5f)*0.2f
                };
                
                p.temperature = SPAWN_TEMP;
                p.age = 0.0f;
                p.maxLife = 0.8f + ((rand()%100)/100.f) * 0.7f;
            }
        }

        glDisable(GL_DEPTH_TEST);
        glUseProgram(bgProg);
        glDrawArrays(GL_TRIANGLES,0,3);

        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);

        perspective(P,45*0.0174533f,1280.f/720.f,0.1f,100);
        lookAt(V);
        rotateY(M,(float)now);
        mul(PV,P,V);
        mul(MVP,PV,M);

        glUseProgram(meshProg);
        glUniformMatrix4fv(mvpLoc,1,GL_FALSE,MVP);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES,0,6);

        // Prepare draw buffer
        DrawPart draw[MAX_PARTS];
        for(int i=0;i<MAX_PARTS;i++) {
            draw[i].p = parts[i].pos;
            draw[i].t = parts[i].temperature;
        }

        glBindBuffer(GL_ARRAY_BUFFER,pVBO);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(draw),draw);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE);
        glUseProgram(partProg);
        glUniformMatrix4fv(pMVP,1,GL_FALSE,MVP);
        glUniform1f(pSize,14);
        glBindVertexArray(pVAO);
        glDrawArrays(GL_POINTS,0,MAX_PARTS);
        glDisable(GL_BLEND);

        glfwSwapBuffers(w);
        glfwPollEvents();
    }
}
