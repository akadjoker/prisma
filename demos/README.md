# prisma demos

Small programs that each show one thing the [prisma](https://github.com/akadjoker/prisma) driver can do. They run on OpenGL 4.6, OpenGL ES 3 and Vulkan: add `vulkan` to the command line for Vulkan, a number to stop after that many frames, and `still` to freeze time.

The demos that load meshes and textures read them from a media folder: set the environment variable `PRISMA_MEDIA` or the CMake option `PRISMA_MEDIA_DIR`. A demo whose feature is missing on the GPU prints a line and exits.

| | | |
|---|---|---|
| ![clear](images/01_clear.png) **01 clear**<br>a window cleared to one colour | ![triangle](images/02_triangle.png) **02 triangle**<br>one coloured triangle | ![cube](images/03_cube.png) **03 cube**<br>a textured cube; `offscreen` draws it to an HDR target first |
| ![two cubes](images/04_two_cubes.png) **04 two cubes**<br>depth testing and several draws sharing one uniform buffer | ![lighting](images/05_lighting.png) **05 lighting**<br>two directional lights | ![texture](images/06_texture.png) **06 texture**<br>a compressed texture with its mip chain and anisotropic filtering |
| ![model](images/07_model.png) **07 model**<br>a mesh file with materials and textures | ![reflection](images/08_reflection.png) **08 reflection**<br>cube map reflection | ![blend and stencil](images/09_blend_stencil.png) **09 blend and stencil**<br>blend modes and stencil masks (keys 1 to 4 and S) |
| ![instancing](images/10_instancing.png) **10 instancing**<br>4000 cubes in one draw call | ![msaa](images/11_offscreen_msaa.png) **11 offscreen msaa**<br>a multisampled offscreen target with resolve | ![particles](images/12_particles.png) **12 particles**<br>16384 particles simulated by a compute shader |
| ![hdr](images/13_hdr.png) **13 hdr**<br>HDR scene, bloom and tone mapping (up and down, B) | ![shadow map](images/14_shadow_map.png) **14 shadow map**<br>shadow mapping with a comparison sampler | ![soldier](images/15_soldier.png) **15 soldier**<br>animated skinned characters |
| ![tessellation](images/16_tessellation.png) **16 tessellation**<br>a Bezier surface on the tessellator (up and down, W) | ![point sprites](images/17_point_sprites.png) **17 point sprites**<br>a geometry shader turns points into quads | |

## Screenshots without a window

The pictures above are taken by the demos themselves, with no window system: configure a tree whose window library is a stub and whose driver draws into an off-screen surface on the real GPU, then run the script.

```sh
cmake -S . -B build-shots -DPRISMA_HEADLESS_DEMOS=ON -DPRISMA_BUILD_TESTS=OFF
cmake --build build-shots
./demos/shots.sh build-shots demos/images
```

Any demo also saves a picture of the frame it is drawing when `PRISMA_SHOT=file.png` is set (and `PRISMA_SHOT_FRAME=n` picks the frame), in a normal window build too.
