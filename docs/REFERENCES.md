# References

## Physics & Simulation

1.  **"How Gravity Sculpts a Candle Flame"**: [Science Paper / Article]
    - Explanation of buoyancy-driven flame shape and Rayleigh-Taylor instability.
2.  **"Curl Noise for Procedural Fluid Flow"** (2007) by Robert Bridson et al.
    - Original paper introducing divergence-free curl noise fields.
    - [Link to Paper](https://www.cs.ubc.ca/~rbridson/docs/curlnoise.pdf)
3.  **"Simulating Fire with Particle Systems"** (Reeves, 1983)
    - Foundational paper on particle-based fire rendering.
    - [Link](https://dl.acm.org/doi/10.1145/357318.357320)

## Mathematics

1.  **Boussinesq Approximation**:
    - [Wikipedia Article](<https://en.wikipedia.org/wiki/Boussinesq_approximation_(buoyancy)>)
    - Simplified model for density-driven flow.

2.  **Perlin Noise**:
    - Ken Perlin's Improved Noise (2002).
    - [Explanation](https://mrl.cs.nyu.edu/~perlin/noise/)

## Graphics Programming

1.  **Blackbody Radiation Color Mapping**:
    - Based on Planck's Law approximation for color temperature.
    - [Color Temperature Chart](https://en.wikipedia.org/wiki/Color_temperature)

2.  **OpenGL / GLSL**:
    - Vertex and Fragment Shader pipeline.
    - Additive Blending (`GL_ONE`, `GL_SRC_ALPHA`) for fire glow.

## Project Structure references

- `src/noise.cpp`: Implementation of 3D Perlin Noise and Curl Noise.
- `src/math_utils.h`: Vector 3D math operations.
