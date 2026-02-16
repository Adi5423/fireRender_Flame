# Algorithm Workflow & Project Structure

## 1. Project Organization

The simulation is modular, separating physics, rendering, and utilities:

```
src/
├── sandbox_main.cpp    # ENTRY POINT: Application loop & Particle System
├── noise.h/cpp         # TURBULENCE: Perlin Noise & Curl Noise Implementation
├── math_utils.h        # UTILITIES: Vec3 struct, Vector Math operators
```

## 2. Execution Workflow

The simulation runs in a continuous loop (`main` function). Each frame follows these steps:

### Step 1: Initialization

1.  **Window Creation**: Setup GLFW window and OpenGL context (4.6 Core).
2.  **Particle Spawning**: Create `MAX_PARTS` (256) particles.
    - Spawn at `FLAME_BASE_Y` (-0.5).
    - Initialize `temperature = 1.0` (Hot).
    - Apply random radial spread within `SPAWN_RADIUS`.

### Step 2: Physics Update (Per Particle)

For each particle $P$:

1.  **Temperature Decay**:
    $$ T*{new} = T*{old} \cdot e^{-cool_rate \cdot dt} $$
    Simulates heat loss to the environment.

2.  **Buoyancy Force**:
    $$ F*{buoy} = \beta \cdot (T - T*{amb}) $$
    Apply upward acceleration proportional to temperature.
    - **Hot particles**: Accelerate upward strongly.
    - **Cool particles**: Rise slowly or drift.

3.  **Turbulence Application**:
    Calculate Vector Potential $\vec{\Psi}$ using `noise3D`.
    Calculate Curl Velocity $\vec{v}_{turb} = \nabla \times \vec{\Psi}$.
    Scale turbulence by temperature: Hotter = More turbulent.
    Apply force: $P.\vec{v} += \vec{v}_{turb} \cdot dt$.

4.  **Shape Constraint**:
    Check if particle is outside the flame cone radius $R(h)$.
    If outside, apply soft spring force pushing inward toward the center axis.

5.  **Integration**:
    $$ P.\vec{pos} += P.\vec{v} \cdot dt $$
    $$ P.age += dt $$

6.  **Respawn Logic**:
    If particle exceeds `maxLife` or height limit:
    - Reset to base position.
    - Reset temperature to 1.0.

### Step 3: Rendering Pipeline

1.  **Data Upload**:
    Copy `particle.pos` and `particle.temperature` to a vertex buffer (VBO).

2.  **Background Pass (bgVS/bgFS)**:
    Draw a full-screen quad with a gradient based on UV coordinates (Blue sky to Black ground).

3.  **Mesh Pass (meshVS/meshFS)**:
    Draw the base geometry (simple platform) with depth testing enabled.

4.  **Particle Pass (partVS/partFS)**:
    - **Blending**: Enable `GL_BLEND` with `GL_ONE` (Additive) for glowing fire.
    - **Vertex Shader**: Set `gl_PointSize` based on temperature (Hotter = Larger). Pass temperature to fragment shader.
    - **Fragment Shader**:
      - Discard pixels outside circle (creates round points).
      - Map `vTemp` to Color Ramp (White -> Yellow -> Orange -> Red -> Black).
      - Output final color with alpha based on distance from center (soft edges).

## 3. Data Structures

### Particle Struct

```cpp
struct Particle {
    Vec3 pos;           // Current 3D position
    Vec3 vel;           // Current velocity vector
    float temperature;  // Heat intensity (0.0 - 1.0)
    float age;          // Time since spawn (seconds)
    float maxLife;      // Total lifespan before respawn
};
```

### Rendering Data Struct

```cpp
struct DrawPart {
    Vec3 p; // Position (attribute 0)
    float t; // Temperature (attribute 1)
};
```

## 4. Key Algorithms

### Curl Noise (Divergence-Free Turbulence)

Instead of standard noise which pushes particles randomly (compressible), we use the **Curl of a Vector Potential**:
$$ \vec{v} = \nabla \times \vec{\Psi} $$
By definition, $\nabla \cdot (\nabla \times \vec{\Psi}) = 0$.
This guarantees the velocity field has **zero divergence**, meaning fluid volume is conserved. This creates realistic swirling vortices that characterize smoke and fire.

### Boussinesq Approximation

We simplify fluid dynamics by ignoring density variations except in the gravity term.
$$ \rho \approx \rho_0(1 - \beta(T - T_0)) $$
Gravity force becomes:
$$ F_g = (\rho - \rho_0)g \approx -\rho_0 \beta (T - T_0) g $$
This allows us to simulate buoyancy with a simple linear term proportional to temperature difference.
