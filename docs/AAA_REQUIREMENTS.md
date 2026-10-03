# prisma — levantamento: o que o backend precisa para gráficos AAA (alvo quase Unreal)

Fontes lidas (só leitura): Godot 4.8-dev (`engines/godot`), Filament, Ogre-Next 3.0, e os loaders `prisma/opengl`. Cada afirmação vem de código lido pelos agentes; "não existe" quer dizer que a pesquisa não encontrou nada. Os níveis abaixo dizem **de onde se porta** e **o que é preciso construir de raiz**.

## 1. Resumo

- Quase tudo o que dá "aspeto AAA" tem código existente para portar: PBR completo, IBL, sombras suaves em cascata, luzes clustered, GI (SDFGI, VoxelGI, irradiance field, VCT), SSR, SSAO/SSIL, volumetric fog, TAA, FSR2, DoF, bloom, tonemap, partículas na GPU, skinning na GPU.
- A maior parte é **compute + imagens de storage + atomics**: o Godot corre assim. Por isso o backend principal tem de ter compute (GL 4.6 e Vulkan têm; GLES 3.1 tem; WebGL2 não).
- O que **não existe em nenhuma referência** (e é o que distingue o Unreal): geometria virtualizada (Nanite), GI tipo Lumen, virtual shadow maps, culling na GPU, ray tracing no renderer, OIT. Há RHI para ray tracing no Godot mas nenhum renderer o usa. Isto constrói-se a partir de técnicas publicadas, sem código para copiar, e é a fase final.

## 2. Níveis

### Nível 1 — portar já (tudo com código)
| Feature | Origem principal |
|---|---|
| BRDF PBR, clear coat, anisotropia, sheen, iridescência, subsurface, cloth, refração | Filament `shaders/src/surface_*.fs`, `MaterialEnums.h` |
| IBL (SH, specular prefiltrado, DFG LUT) | Filament `libs/ibl`, `surface_light_indirect.fs`; Godot `octmap` |
| Luzes clustered | Godot `cluster_builder_rd` + `cluster_render.glsl` (GPU, subgroup); Filament `Froxelizer.cpp` (CPU) |
| Luzes de área LTC, IES, decals | Ogre-Next `AreaLights_LTC_piece_ps.any`, `LightProfiles`; Godot `area_lights_inc.glsl`, `decal_data_inc.glsl` |
| Sombras: CSM, atlas omni/spot, PCSS | Godot `scene_forward_lights_inc.glsl`, `light_storage.cpp`; Filament `ShadowMapManager.cpp` (PCF/VSM/PCSS/contacto) |
| HDR, exposição automática, bloom, tonemap, color grading | Godot `luminance`, `tonemap.glsl`; Filament `ColorGrading.cpp`, `bloom` |
| AA e temporal: TAA, FXAA, SMAA, MSAA com resolve, upscaling (FSR1/FSR2, SGSR) | Godot `taa`, `smaa`, `fsr2`, `resolve.glsl`; Filament `taa`, `fsr`, `sgsr` |
| SSAO (SAO/GTAO/ASSAO), SSR, DoF, lens flare, fog de altura | Godot `ss_effects.cpp`, `screen_space_reflection*.glsl`, `bokeh_dof`; Filament `ssao`, `dof`, `flare`, `surface_fog.fs` |
| Skinning e morph na GPU, instancing, partículas na GPU | Godot `skeleton.glsl`, `particles.glsl` (+ sort); Filament skinning/morph |
| Atmosfera e fog (lâmpadas atravessam a névoa) | Ogre-Next `Components/Atmosphere`, `800.PixelShader_piece_ps.any:925-934` |

### Nível 2 — com código, mais pesado (compute, imagens 3D, atomics)
| Feature | Origem | Pede |
|---|---|---|
| SDFGI | Godot `environment/gi.cpp`, `sdfgi_*.glsl` | texturas 3D, arrays 2D, imageAtomicOr, views com formato reinterpretado, dispatch indirecto |
| VoxelGI | Godot `voxel_gi*.glsl` | 3D RGBA8 storage, SSBO |
| Irradiance field (estilo DDGI), VCT, instant radiosity | Ogre-Next `Components/Hlms/Pbs/src/IrradianceField`, `Vct`, `InstantRadiosity` | compute, UAV com loads tipados, 3D |
| Volumetric fog / fog volumes | Godot `environment/fog.cpp`, `volumetric_fog*.glsl` | froxels em 3D RGBA16F, atomics 32-bit (ou SSBO) |
| SSIL, SSS, motion vectors | Godot `ssil`, `subsurface_scattering.glsl`, `motion_vector*` | compute, MRT extra |
| Reflexões planares, probes paralax | Ogre-Next `PlanarReflections`, `Cubemaps` | user clip planes, cube array |
| Lightmaps (bake em GPU) | Godot `modules/lightmapper_rd` | compute path tracing sem RT |

### Nível 3 — sem código nas referências (construir de raiz)
| Feature | Estado nas referências | Como se faria (requisitos de backend) |
|---|---|---|
| Geometria virtualizada (tipo Nanite) | não existe (Godot só tem LODs discretos) | clusters/meshlets, culling e rasterização em compute, ou mesh shaders; precisa de indirect count, atomics 64-bit, buffers grandes |
| GI tipo Lumen | não existe | SDF global + radiance cache/screen probes em compute; SDFGI do Godot dá a base |
| Virtual shadow maps | não existe (sparse desativado no Godot) | sparse textures + paginação em compute |
| Culling na GPU, Hi-Z, multi-draw indirect count | não existe (Godot: oclusão em CPU/Embree) | compute + MDI/indirect count (GL 4.6 e Vulkan) |
| OIT | não existe | linked list ou weighted blended (Filament tem WBOIT no RTSS? não verificado) |
| Ray tracing no renderer | Godot tem a API na RHI, nenhum passe a usa | só Vulkan (ray query/pipeline); GL não tem |
| Bindless / descriptor indexing | não existe | GL: `ARB_bindless_texture`; Vulkan: descriptor indexing |
| Async compute, multi-queue | não existe | só Vulkan |

## 3. Requisitos da RHI (o contrato do backend)

Marcadores: **LEVE** = fácil; **EXT** = precisa de extensão/ligação nova no loader.

### 3.1 Recursos
- Texturas: 1D/2D/3D/cube/array/cube array; mips; MSAA 2/4/8; views por slice e views com formato reinterpretado (R32UI↔E5B9G9R9, R16UI↔RGBA4); swizzle; texturas transitórias/descartáveis (mobile).
- Formatos: R8, RG8, RGBA8(+sRGB), R16F/RG16F/RGBA16F, R32F, R11G11B10F, RGB10A2, E5B9G9R9, R16/R32 UINT/SINT, RGBA32F; depth D16/D32F/D24S8/D32FS8; BC1–7 (inclui BC6H), ETC2, ASTC.
- Buffers: vertex, index, uniform (offset dinâmico), storage (offset dinâmico), indirect (+ dispatch indirect), texel buffer; buffers persistentes mapeados com N frames em voo; um buffer pode ser vertex **e** storage (skinning em compute).
- Samplers: compare, anisotropia, LOD bias; sampler separado da textura.

### 3.2 Pipelines e passes
- Raster: blend por alvo, depth/stencil, cull, depth bias (+clamp), **depth clamp**, alpha-to-coverage, sample shading, polygon mode, line width, specialization constants, patches (tess) opcional.
- Compute: shared memory, barriers, atomics 32-bit em imagens e buffers, **subgroup** (ballot, vote, aritmética), dispatch indirect; atomics 64-bit para Nível 3.
- Render pass: MRT (≥8), depth-only, passe sem anexos com side effects (cluster build do Godot), render por slice/layer/face, input attachments/subpasses (mobile), resolve de MSAA e de depth, viewport/layer a partir de qualquer stage, multiview opcional.
- Draw: instanced, base-vertex, base-instance, indirect, multi-draw indirect, **indirect count**.
- Push constants (~128 B), UBO ≥ 16 KiB (idealmente 64 KiB), SSBO grandes, ≥ 48 amostras por stage para Forward+ clustered (senão cai para o caminho "mobile").

### 3.3 Sincronização e diagnóstico
- Barreiras automáticas por rastreio de uso de recursos (grafo de render do Godot: `rendering_device_graph.*`), com fallback `full_barrier` para depurar.
- Fences, timestamp queries (GPU timers), occlusion queries, debug labels/markers, pipeline cache, compilação de shaders em paralelo.
- Capabilities consultáveis (limites, subgroup, atomics, formatos por uso).

## 4. Estado dos loaders (do que existe em `prisma/opengl`)

Resumo da auditoria (detalhe nos relatórios dos agentes):

| Capacidade | OpenGL.h (GL 4.6) | OpenGLES3.h (ES 3.x) |
|---|---|---|
| DSA | sim (4.5) | não existe em ES |
| Compute / SSBO / imagens / atomic counters / barreiras | sim | sim (ES 3.1) |
| Multi-draw indirect | sim | **falta** (EXT_multi_draw_indirect) |
| Indirect count (4.6) | sim | **falta** |
| Base-instance / base-vertex | sim | só base-vertex em ES 3.2; falta EXT_base_instance |
| Buffers persistentes (`glBufferStorage`) | sim | **falta** (EXT_buffer_storage) |
| `glSpecializeShader` (SPIR-V) | **sim** (4.6) | não existe em ES |
| `glClipControl` (reverse-Z) | sim | **falta** (EXT_clip_control) |
| Depth clamp | sim | **falta** (EXT_depth_clamp) |
| Cube array | sim | constante sim, sem flag por extensão |
| MSAA texturas + resolve | sim | sim (ES 3.1+) |
| BC1–7 / ETC2 / ASTC | sim | BC por extensão, RGTC falta |
| Float render targets | core 3.0 | flag `EXT_color_buffer_float` (mistura half e float) |
| Timer queries | sim | via EXT_disjoint_timer_query (incompleto) |
| **Bindless** (`ARB_bindless_texture`) | **falta** | falta |
| **Sparse** textures/buffers | **falta** | falta |
| **Subgroup** (ballot/vote) | **falta** (só flag/extensão, sem funções) | falta |
| Atomics 64-bit, mesh shaders (NV), conservative raster | **falta** | falta |
| Compilação de shaders em paralelo (`KHR_parallel_shader_compile`) | **falta** | falta |
| Flags de extensão | só 15, quase legadas; flags de timer/sRGB/float só em contextos < 3.0 | 15; PVRTC declarada sem uso; ETC1 não exposta |
| Fallbacks por extensão (DSA, buffer_storage, clip_control…) | **não há** (só por versão do contexto) | n/a |

O loader desktop está completo para o núcleo 4.6; falta sobretudo detecção/ligação de extensões (subgroup, bindless, sparse, parallel compile) e um `isExtensionSupported` público.

## 5. O que o desktop pode e não pode (honestidade técnica)

| Alvo | Consegue | Não consegue |
|---|---|---|
| **GL 4.6 (desktop)** | Nível 1 e 2 completos; Nível 3 em compute: culling GPU, meshlets por compute, MDI+count, sparse (ARB), bindless (ARB), subgroup (ext), SDF GI | Ray tracing; async compute; descriptor indexing nativo; controlo explícito de memória/barreiras (o driver decide); mesh shaders só NVIDIA via extensão |
| **Vulkan (desktop + Android)** | Tudo do GL 4.6, mais ray query/pipeline, mesh shaders (EXT), async compute, bindless por descriptor indexing, sincronização explícita, multiview, VRS | — |
| **GLES 3.1** | Nível 1 e parte do Nível 2 (compute existe); sem MDI-count, sem persistent buffers sem EXT | Quase todo o Nível 3; WebGL2 não tem compute |

Conclusão: "quase Unreal" cobre-se por camadas. Com GL 4.6 chega-se a um aspeto AAA forte (Nível 1 + 2 e parte do 3 em compute). O que é mesmo exclusivo de Vulkan é ray tracing, async compute e sincronização explícita; o desenho da interface do backend (`BACKENDS.md`) já está pensado para o Vulkan receber isto sem mexer no render.

## 6. Ordem de construção proposta

1. RHI base + GL 4.6: texturas, UBO/SSBO, render pass, PSO, descritores, compute, barreiras, queries.
2. Núcleo visual (Nível 1): PBR + IBL + CSM + HDR/tonemap/bloom + luzes clustered + fog. Marcos já definidos em `ARCHITECTURE.md`.
3. Temporal e pós: TAA, SSAO, SSR, DoF, MSAA/FSR2.
4. GI e volumétricos (Nível 2): SDFGI primeiro (código mais completo), depois volumetric fog, irradiance field.
5. Loader: adicionar subgroup, bindless, sparse, parallel compile, `isExtensionSupported`.
6. Nível 3: culling na GPU + MDI-count, depois meshlets e virtual shadow maps.
7. Vulkan: backend novo com o mesmo contrato; ray tracing e async compute como extras.

## 7. Pontos em aberto
- Atomics 64-bit e mesh shaders só se o hardware alvo (NVIDIA?) o permitir; precisa de decisão sobre hardware mínimo.
- Qual GI como primeiro (SDFGI do Godot vs irradiance field do Ogre-Next).
- Se o Forward+ clustered usa o caminho GPU do Godot (subgroup) ou o CPU do Filament, conforme o hardware.
- Não verificado nos relatórios: o corpo das shaders de LTC, do Physical Sky e do FXAA; WBOIT do Filament; detalhes de D3D12/Metal.

## 8. Medido nesta máquina (Radeon Renoir, Mesa, GL 4.6) com o loader melhorado

Todas as seguintes extensões dão `true` e as funções carregam: `ARB_bindless_texture`, `ARB_sparse_texture` (+2), `ARB_sparse_buffer`, `KHR_shader_subgroup` (tamanho 64, todas as features: basic, vote, arithmetic, ballot, shuffle, shuffle relative, clustered, quad), `ARB_shader_ballot`, `ARB_gpu_shader_int64`, `NV_shader_atomic_int64`, `ARB_gl_spirv`, `ARB_indirect_parameters`, `KHR_parallel_shader_compile`, `ARB_compute_variable_group_size`, `ARB_direct_state_access`, `ARB_buffer_storage`, `ARB_clip_control`, `ARB_multi_draw_indirect`, `ARB_shader_viewport_layer_array`.

Não existem aqui: `NV_mesh_shader`, `NV_conservative_raster`, `INTEL_conservative_rasterization`, `ARB_fragment_shader_interlock`, `AMD_shader_ballot`.

Consequência para o Nível 3: culling na GPU com multi-draw indirect count, bindless, sparse (virtual shadow maps) e atomics de 64 bits (rasterização por compute de meshlets) são possíveis em GL 4.6 nesta máquina. Mesh shaders não.
