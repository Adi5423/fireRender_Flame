Physically-Based Flame Simulation Ready!

FEATURES:
- Temperature-Driven Buoyancy (hot gases rise)
- Cone-Shaped Confinement (flame structure)
- Curl Noise Turbulence (realistic flickering)
- Blackbody Color Rendering (blue-white core → orange → smoke)
- Volumetric Appearance (Gaussian soft particles)
- Realistic Formation Animation (watch flame gradually form!)

HOW TO RUN:
To start the simulation, run this command in your terminal:
.\build\Sandbox.exe

WHAT TO EXPECT:
- The flame will gradually form over 2.5 seconds
- Watch particles spawn progressively to build the flame
- After formation, the flame becomes stable and persistent
- No more continuous "generation" - just a beautiful, stable flame!

CONTROLS:
- Right Click + Mouse: Look around
- Enjoy the fire!

FILES:
- src/sandbox_main.cpp: Main application & physics loop
- src/noise.cpp: Turbulence implementation
- src/math_utils.h: Math helpers

DOCUMENTATION:
Check the `docs/` folder for deeper details:
- docs/TECH_REPORT.md: Detailed physics and equations
- docs/ALGO_WORKFLOW_AND_STRUCTURE.md: How the code works
- docs/REFERENCES.md: Research papers and sources
