# octonion_fractals

Vulkan ray-marched 3D fractal zoo, with a heavy octonion bias. One binary, **39
fractals**, Tab cycles between them. Push-constants only (no descriptor sets,
no textures). HUD with current fractal name + controls is drawn by the same
fragment pass using a baked 3×5 bitmap font.

## Fractal roster

**Octonion z<sup>n</sup> + c Julias (𝕆 via Cayley–Dickson):** z<sup>2</sup>,
z<sup>3</sup>, z<sup>4</sup>, z<sup>5</sup>, z<sup>6</sup>, z<sup>7</sup>, z<sup>8</sup>

**Octonion z<sup>n</sup> + c Mandelbrots:** z<sup>2</sup>, z<sup>3</sup>,
z<sup>4</sup>, z<sup>5</sup>

**Octonion exotics:** tricorn (`conj(z)²+c`), tricorn z³, burning ship, sin(z)+c,
cos(z)+c (truncated Taylor series, both)

**Octonion slice / hybrid variants:** alternate (e<sub>3</sub>,e<sub>4</sub>,e<sub>5</sub>)
slice, skew 8-basis slice, z²/z³ alternating, octonion-with-mandelbox fold, two
coupled octonions

**Quaternion (ℍ) family:** Julia z², Mandelbrot z², Julia z³, Julia z⁴, tricorn

**Mandelbulbs:** powers 2, 3, 8, 16 plus Juliabulb

**Mandelboxes:** scale −2, scale −3

**Folding IFS:** Sierpinski tetrahedron, Menger sponge, KIFS dodecahedral, KIFS
icosahedral

**Other:** Apollonian gasket (3D), gyroid TPMS

## Build & run

```sh
cd build
cmake ..
make -j
./octonion_fractals     # runs from any cwd; shaders are loaded next to the exe
```

CMake's shader rule globs `shaders/*.{vert,frag}` with `CONFIGURE_DEPENDS`, so
new shaders land automatically — but you still need to add the fractal to
`kFractals[]` in `src/main.cpp` and to `TITLES` in `shaders/gen_font.py`, then
regenerate `shaders/font_data.glsl`:

```sh
python3 shaders/gen_font.py > shaders/font_data.glsl
```

Requires: Vulkan 1.2+, GLFW 3, glslc, a C++17 compiler. Validation layers are
on in non-Release builds.

## Controls

### Navigation
| input              | effect                                       |
|--------------------|----------------------------------------------|
| `Tab` / `Shift+Tab`| next / previous fractal                      |
| left-mouse drag    | rotate view                                  |
| scroll             | zoom (FOV)                                   |
| `Ctrl` + scroll    | dolly forward / back                         |
| `[` / `]` (`-`/`=`)| keyboard zoom                                |
| `W`/`A`/`S`/`D`    | fly horizontally                             |
| `Q`/`E`            | down / up                                    |
| `Shift` (held)     | move 3× faster                               |

### Tuning (hold for continuous, release to stop)
| input              | effect                                       |
|--------------------|----------------------------------------------|
| `,` / `.`          | paramZ down / up (iter, power, scale...)     |
| `J` / `K`          | paramW down / up (bailout, fold, rotation...)|
| `O` / `P`          | palette phase shift                          |

### Modes
| input              | effect                                       |
|--------------------|----------------------------------------------|
| `R`                | randomize current fractal's parameters       |
| `Space`            | pause animation (held `c`)                   |
| `T`                | toggle `c`-animation entirely                |
| `Backspace`        | reset paramZ/paramW/c/camera to fractal default |
| `Y`                | print current state to stdout (paramZ, paramW, c, camera) |
| `H`                | toggle full controls overlay                 |
| `F12`              | save PNG screenshot to cwd (`octonion_<idx>_<unix_ms>.png`) |
| `Esc`              | quit                                         |

Per-fractal meaning of paramZ / paramW:

| family            | paramZ                 | paramW                  |
|-------------------|------------------------|-------------------------|
| octonion / quaternion Julia & Mandelbrot | iter count | bailout (|z|²)  |
| octonion sin / cos | iter count            | bailout                 |
| Mandelbulb        | power n                | bailout                 |
| Juliabulb         | power n                | bailout                 |
| Mandelbox         | fold scale (negative)  | fold limit              |
| Sierpinski        | iter count             | rotation angle (rad)    |
| Menger sponge     | iter count             | —                       |
| KIFS dodec/icos   | iter count             | —                       |
| Apollonian gasket | iter count             | inversion scale         |
| Gyroid TPMS       | frequency multiplier   | thickness               |

## Push-constant layout (128 bytes)

| field       | meaning                                                         |
|-------------|-----------------------------------------------------------------|
| `cam_pos`   | xyz position, w = `tan(fov/2)`                                  |
| `cam_right` | xyz right basis                                                 |
| `cam_up`    | xyz up basis                                                    |
| `cam_fwd`   | xyz forward basis                                               |
| `c_lo`      | Julia `c` (low 4 components)                                    |
| `c_hi`      | Julia `c` (high 4 for octonion); `c_hi.w` is palette phase (incl. user offset) |
| `params`    | `(time, aspect, paramZ, paramW)` — paramZ/paramW per fractal    |
| `hud`       | `(fractal_index, show_help, paused, _)`                         |

Each fragment shader defines `float scene_de(vec3 p, out float trap)`;
`common.glsl` provides the ray march, normal estimator, palette, lighting,
fog, tonemap, and HUD compositing.
