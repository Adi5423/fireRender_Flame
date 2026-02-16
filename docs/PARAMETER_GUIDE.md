# Flame Simulation - Tweakable Parameters Guide

This guide shows you which parameters to modify to achieve different flame effects.

---

## 🔥 Formation Behavior

### Formation Duration

**Location:** Line 88 in `sandbox_main.cpp`

```cpp
const float FORMATION_DURATION = 2.5f;  // Seconds to complete formation
```

- **Decrease** (e.g., `1.0f`): Faster ignition (quick whoosh)
- **Increase** (e.g., `5.0f`): Slower ignition (gradual buildup)
- **Recommended Range:** 1.0 - 5.0 seconds

---

## 🎨 Visual Appearance

### Particle Size

**Location:** Line 483 in `sandbox_main.cpp`

```cpp
glUniform1f(pSize, 28);  // Larger particles for volumetric appearance
```

- **Decrease** (e.g., `20`): Smaller, more defined particles
- **Increase** (e.g., `40`): Larger, more diffuse/smoky appearance
- **Recommended Range:** 20 - 50

### Particle Count

**Location:** Line 81 in `sandbox_main.cpp`

```cpp
constexpr int MAX_PARTS = 256;
```

- **Decrease** (e.g., `128`): Less dense flame (better performance)
- **Increase** (e.g., `512`): More dense, volumetric flame (lower FPS)
- **Recommended Range:** 128 - 512
- **Note:** Higher counts = smoother, more realistic, but slower

### Gaussian Softness

**Location:** Line 260 in `sandbox_main.cpp` (inside fragment shader)

```glsl
float gaussian = exp(-d*d*8.0);
```

- **Decrease exponent** (e.g., `4.0`): Softer edges, more diffuse
- **Increase exponent** (e.g., `12.0`): Sharper edges, more defined
- **Recommended Range:** 4.0 - 12.0

---

## ⚗️ Physics Parameters

### Buoyancy Strength

**Location:** Line 92 in `sandbox_main.cpp`

```cpp
constexpr float THERMAL_BUOYANCY = 15.0f;
```

- **Decrease** (e.g., `10.0f`): Slower rising (lazy flame)
- **Increase** (e.g., `20.0f`): Faster rising (aggressive flame)
- **Recommended Range:** 8.0 - 25.0

### Cooling Rate

**Location:** Line 99 in `sandbox_main.cpp`

```cpp
constexpr float COOLING_RATE = 0.8f;
```

- **Decrease** (e.g., `0.5f`): Slower cooling = Taller flame
- **Increase** (e.g., `1.2f`): Faster cooling = Shorter flame
- **Recommended Range:** 0.3 - 1.5

### Turbulence Intensity

**Location:** Line 420 in `sandbox_main.cpp`

```cpp
Vec3 turbulence = curlNoise(p.pos * 2.0f, (float)now * 1.5f) * (0.5f + p.temperature);
p.vel = p.vel + turbulence * dt * 2.0f;  // Scale factor: 2.0f
```

- **Decrease scale** (e.g., `1.0f`): Less flickering (calm flame)
- **Increase scale** (e.g., `3.0f`): More flickering (turbulent flame)
- **Recommended Range:** 0.5 - 4.0

---

## 📐 Flame Shape

### Spawn Radius (Base Width)

**Location:** Line 106 in `sandbox_main.cpp`

```cpp
constexpr float SPAWN_RADIUS = 0.15f;
```

- **Decrease** (e.g., `0.08f`): Narrow base (candle flame)
- **Increase** (e.g., `0.25f`): Wide base (torch/bonfire)
- **Recommended Range:** 0.05 - 0.3

### Flame Cone Taper

**Location:** Line 150 in `sandbox_main.cpp` (inside `applyFlameShape`)

```cpp
float maxRadius = spawnRadius * (1.0f - height / 3.0f);
```

- **Decrease divisor** (e.g., `/2.0f`): Steeper taper (narrow tip)
- **Increase divisor** (e.g., `/5.0f`): Gentler taper (wider tip)
- **Recommended Range:** 2.0 - 6.0

### Cone Push Strength

**Location:** Line 159 in `sandbox_main.cpp`

```cpp
float pushStrength = excess * 3.0f;  // Spring constant
```

- **Decrease** (e.g., `2.0f`): Looser confinement (wider flame)
- **Increase** (e.g., `5.0f`): Tighter confinement (narrower flame)
- **Recommended Range:** 1.0 - 6.0

---

## 🌈 Color Customization

### Core Temperature Threshold

**Location:** Lines 233-237 in `sandbox_main.cpp` (fragment shader)

```glsl
if (t > 0.95) {
    // Ultra-hot core: Blue-white tint
    return mix(vec3(1.0, 1.0, 1.0), vec3(0.7, 0.85, 1.0), (t-0.95)*20.0);
}
```

- **Lower threshold** (e.g., `0.90`): More blue-white regions
- **Raise threshold** (e.g., `0.98`): Less blue-white (more traditional yellow)

### Color Zones

Modify the color values in the `enhancedFlameColor()` function:

```glsl
// Example: Make flame more orange/red
if (t > 0.5) {
    // Change from vec3(1.0, 0.45, 0.05) to vec3(1.0, 0.3, 0.0)
    return mix(vec3(1.0, 0.3, 0.0), vec3(1.0, 0.95, 0.7), (t-0.5)*3.33);
}
```

---

## 🎯 Preset Configurations

### Candle Flame

```cpp
MAX_PARTS = 128
FORMATION_DURATION = 1.5f
SPAWN_RADIUS = 0.08f
THERMAL_BUOYANCY = 12.0f
COOLING_RATE = 1.0f
Particle size = 22
Turbulence scale = 1.5f
```

### Torch/Bonfire

```cpp
MAX_PARTS = 384
FORMATION_DURATION = 3.0f
SPAWN_RADIUS = 0.25f
THERMAL_BUOYANCY = 18.0f
COOLING_RATE = 0.6f
Particle size = 32
Turbulence scale = 2.5f
```

### Gas Burner (Blue Flame)

```cpp
MAX_PARTS = 256
FORMATION_DURATION = 0.8f
SPAWN_RADIUS = 0.12f
THERMAL_BUOYANCY = 20.0f
COOLING_RATE = 1.5f
Particle size = 20
Turbulence scale = 1.0f
// Lower core threshold to 0.85 for more blue
```

---

## 🛠️ Workflow for Experimentation

1. **Edit** the parameter in `sandbox_main.cpp`
2. **Save** the file
3. **Rebuild**:
   ```powershell
   cd build
   cmake --build .
   ```
4. **Run**:
   ```powershell
   .\build\Sandbox.exe
   ```
5. **Observe** the result
6. **Iterate** until satisfied

---

## 💡 Pro Tips

### For Better Performance

- Reduce `MAX_PARTS`
- Decrease particle size
- Lower turbulence frequency

### For More Realism

- Increase `MAX_PARTS` to 384-512
- Add slight randomness to spawn temperature
- Vary particle lifetime more

### For Artistic Control

- Adjust color zones to match your desired palette
- Modify core temperature for hotter/cooler appearance
- Change formation curve (line 345: `easedProgress` calculation)

---

## 🔬 Advanced: Formation Curve

To change how particles spawn during formation, modify line 345:

```cpp
// Current: Ease-out (slow → fast → slow)
float easedProgress = 1.0f - powf(1.0f - progress, 3.0f);

// Alternative: Linear
float easedProgress = progress;

// Alternative: Ease-in (slow start)
float easedProgress = powf(progress, 2.0f);

// Alternative: Ease-in-out (smooth both ends)
float easedProgress = progress < 0.5f
    ? 2.0f * progress * progress
    : 1.0f - powf(-2.0f * progress + 2.0f, 2.0f) / 2.0f;
```

---

**Remember:** After any changes, rebuild before running!

```powershell
cd build && cmake --build . && cd .. && .\build\Sandbox.exe
```
