# Flame Simulation: Technical Report

## 1. Introduction

This project implements a physically-inspired, real-time 3D flame simulation using OpenGL and C++. Unlike traditional particle systems that rely on random forces or texture sheets, this simulation models the **fluid dynamics** of fire using simplified physical laws.

The core goal is to replicate the behavior of a laminar/transitional diffusion flame (like a candle or torch) where:

- Hot gases rise due to buoyancy.
- The flame structure is governed by air entrainment.
- Turbulence arises from fluid instability.

## 2. Physical Foundation

### 2.1 Buoyancy (The Driving Force)

The primary driver of flame motion is **buoyancy**. Hot gases produced by combustion are significantly less dense than the surrounding cool air. Gravity pulls the dense cool air down, which displaces the hot gas upward.

We model this using the **Boussinesq Approximation**, which ignores density differences except in the gravity term:

$$ F*{buoyancy} = -\beta \cdot g \cdot (T - T*{ambient}) $$

- $\beta$: Coefficient of thermal expansion
- $g$: Gravity vector ($0, -9.8, 0$)
- $T$: Particle temperature
- $T_{ambient}$: Ambient air temperature

In our simulation code:

```cpp
float buoyancy = THERMAL_BUOYANCY * (p.temperature - AMBIENT_TEMP);
p.vel.y += buoyancy * dt;
```

This ensures that hotter particles accelerate upward faster.

### 2.2 Thermodynamics (Cooling)

As the hot gas rises, it mixes with cool air and radiates heat. We model this as **exponential cooling**:

$$ T(t) = T_0 \cdot e^{-k \cdot t} $$

- $k$: Cooling rate constant
- $t$: Time

In implementation:

```cpp
p.temperature *= expf(-COOLING_RATE * dt);
```

This creates a natural gradient where the flame is hottest at the base and cools as it rises, eventually becoming smoke.

## 3. Flame Structure (Cone Geometry)

A real flame creates a "cone" shape because the rising column of hot gas accelerates, stretching the volume, while cool air is entrained from the sides, creating a boundary layer.

We enforce this structure using a **Soft Constraint Field**: R(h)
The maximum allowed radius $R$ decreases as height $h$ increases:

$$ R(h) = R*{base} \cdot (1 - \frac{h}{H*{max}}) $$

Particles moving outside this radius are subjected to a spring-like restoring force pushing them back toward the central axis.

## 4. Turbulence (Curl Noise)

Real flames flicker due to turbulence (vorticity). Standard "Perlin Noise" creates random scalar values, but applying them directly to velocity results in unrealistic "compressible" motion (particles bunching up or spreading apart unnaturally).

We use **Divergence-Free Curl Noise** to simulate incompressible fluid accumulation.
We start with a vector potential field $\vec{\Psi}$ (3 uncorrelated noise fields):

$$ \vec{\Psi}(x,y,z) = (N_1, N_2, N_3) $$

The velocity perturbation is the curl of this potential:

$$ \vec{v}\_{turb} = \nabla \times \vec{\Psi} $$

Mathematically, $\nabla \cdot (\nabla \times \vec{\Psi}) = 0$ is always true. This guarantees that the turbulent flow has **zero divergence**, conserving mass and creating realistic swirling vortices.

## 5. Visual Rendering (Blackbody Radiation)

To render the flame realistically, we map the particle temperature to color using a **Blackbody Radiation** approximation:

- **T > 0.8 (Combustion Zone)**: White/Bright Yellow (Incandescence of soot)
- **T > 0.5 (Mid Flame)**: Orange/Yellow
- **T > 0.2 (Upper Flame)**: Red/Dark Orange
- **T < 0.2 (Smoke)**: Gray/Black with low opacity

The rendering uses **Additive Blending** (`GL_ONE`, `GL_SRC_ALPHA`) to simulate light emission. Overlapping particles naturally accumulate brightness, creating a "glow" effect at the core.

## 6. Conclusion

By combining these physical principles—Buoyancy, Thermodynamics, Continuity (Curl Noise), and Radiation—we achieve a visually convincing flame without heavy fluid solvers. The result is deterministic, controllable, and physically grounded.
