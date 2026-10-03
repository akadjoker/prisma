# prisma — arquitetura do render e dos backends

Código em C++14 sem `std`: contentores e utilitários vêm da lib do utilizador `ct` (ver secção 11). Referências (só leitura): Filament `tmp/filament`, Ogre-Next `tmp/Ogre-Next`. Cada módulo abaixo diz de onde se porta.

## 1. Camadas

```
 app / simulador / loaders do utilizador
            |
   ┌────────▼─────────┐
   │  scene  (Camera, Mesh, Light, Material instances, Environment)
   └────────┬─────────┘
   ┌────────▼─────────┐
   │  render          (FrameGraph + passes: shadow, depth, color(PBR),
   │                   IBL, lights, fog, bloom, tonemap, AA)
   └────────┬─────────┘
   ┌────────▼─────────┐
   │  shader system   (fonte GLSL única -> variantes -> SPIR-V -> por backend)
   └────────┬─────────┘
   ┌────────▼─────────┐
   │  RHI             (Device: recursos, comandos, estados, capabilities)
   └──┬──────┬──────┬─┘
      │      │      │
  GL 4.6  GLES 3.1  Vulkan (desktop + Android)
      └──────┴──────┘
   ┌────────▼─────────┐
   │  platform        (SDL2: janela, input, contexto/superfície; Android)
   └──────────────────┘
```

Regra: cada camada só conhece a de baixo. O render nunca inclui código de um backend.

## 2. RHI (src/rhi)

**Modelo: classes, como no Ogre-Next.** A RHI é um conjunto de classes abstratas com uma implementação por backend (`GLRenderSystem`, `VulkanRenderSystem`, ...), sem handles na API pública.

| Classe | Papel | Origem (Ogre-Next, `OgreMain/include`) |
|---|---|---|
| `RenderSystem` | Interface do backend: janela, estados, passes, draw, capabilities | `OgreRenderSystem.h` |
| `VaoManager` | Cria e gere buffers (vertex/index/uniform/storage) e VAOs | `Vao/OgreVaoManager.h` |
| `TextureGpu` / `TextureGpuManager` | Texturas (2D, array, cube, 3D, render targets) e o seu ciclo de vida | `OgreTextureGpu.h`, `OgreTextureGpuManager.h` |
| `RenderPassDescriptor` | Anexos, load/store, clear de um passe | `OgreRenderPassDescriptor.h` |
| `HlmsPso` | Estado de pipeline compilado (shaders + blend + depth + raster) | `OgreHlmsPso.h` |
| `Root` | Ponto de entrada, cria o RenderSystem e os managers | `OgreRoot.h` |

Um objeto é uma instância da classe; o dono (o manager) destrói-o. Para posse partilhada onde for mesmo preciso usa-se `ct::Rc`/`ct::Unique`, e para ligações que não podem ficar penduradas `ct::Weak`.

**Onde os handles ainda fazem sentido (interno, nunca na API):** índices de recursos dentro de um manager (`ct::SlotMap`) para guardar referências baratas e seguras em listas de draw ou no frame graph. O utilizador nunca os vê.

**Duas decisões que valem para os 3 backends**
1. **Estilo de comandos explícito** (render pass com anexos, load/store, barreiras implícitas pela RHI). É o que o Vulkan exige; o GL traduz para framebuffers e `glInvalidateFramebuffer`. Origem: Filament `RenderPassParams` / `beginRenderPass`.
2. **Descritores agrupados** (sets de UBO/samplers) em vez de binding slot a slot. Origem: Filament `GLDescriptorSet.*`, `BindingMap.h` no backend GL; Vulkan nativo.

**Capabilities:** estrutura preenchida por cada backend na criação (compute, SSBO, formatos HDR renderáveis, MSAA, cube array, aniso, tamanho máx. UBO, imagens float lineares...). O render escolhe caminhos por capability e nunca por nome de backend.

**Recursos:** Buffer (vertex/index/uniform/storage/indirect), Texture (2D, 2D array, cube, 3D; mips; sampleCount; usage: sampled/storage/color-attach/depth-attach), Sampler (filter, wrap, compare, aniso), Program (módulo SPIR-V ou fonte por stage), Pipeline gráfico e de compute, RenderTarget (MRT, depth, mip e layer por anexo), Fence/Query (timer).

## 3. Backends

| | **GL 4.6** (desktop) | **GLES 3.1** (mobile / web*) | **Vulkan 1.3** (desktop + Android) |
|---|---|---|---|
| Papel | Backend principal para iterar depressa | Mobile/web com compute e SSBO | Backend moderno e final |
| Shaders | SPIR-V via `GL_ARB_gl_spirv` (`glSpecializeShader`) se disponível, senão GLSL gerado por SPIRV-Cross | GLSL ES gerado por SPIRV-Cross (ES não aceita SPIR-V) | SPIR-V direto |
| Compute / SSBO | sim | sim (ES 3.1) | sim |
| Contexto | SDL2 + loader próprio | EGL (SDL2 / Android) | VkInstance/Device, swapchain |
| Notas | DSA, buffers persistentes | Sem cube array nem depth clamp; float RT por extensão | Alocador de memória, sincronização, descriptor pools |
| Origem | Filament `backend/src/opengl/*` (OpenGLDriver, OpenGLContext, OpenGLState, ShaderCompilerService) | idem, caminhos ES | Filament `backend/src/vulkan/*` |

\* WebGL2 é ES 3.0, sem compute. Web só entra como perfil reduzido (sem compute) ou via WebGPU no futuro; não é alvo da v1.

**Ordem de implementação:** GL 4.6 primeiro (já arranca na máquina), depois Vulkan (escrito já com arrays, layers, MSAA, formatos comprimidos, coisas que o backend antigo não tinha), depois GLES 3.1 (derivado do GL: mesma lógica com limites de ES).

## 4. Sistema de shaders (src/shader)

**Objetivo:** escrever cada técnica uma única vez e correr nos 3 backends.

Pipeline (portado de Filament `libs/filamat`: `MaterialBuilder.cpp`, `MaterialVariants.cpp`, `GLSLPostProcessor.cpp`, `SpirvFixup.cpp`):

```
 fonte GLSL (Vulkan-style, 460) + defines/variantes
        │  (pré-processador de peças: @property/@piece/@insertpiece,
        │   mecanismo de Ogre-Next OgreHlms.cpp)
        ▼
 glslang  ──►  SPIR-V  ──►  spirv-opt / fixups
        │                      │
        ▼                      ├─► Vulkan: usa direto
   reflexão (SPIRV-Reflect)    ├─► GL 4.6: glSpecializeShader (ou SPIRV-Cross -> GLSL 460)
                               └─► GLES 3.1: SPIRV-Cross -> GLSL ES 310
```

- **Variantes:** conjunto ordenado de propriedades (`has_normal_map`, `shadow_cascades=4`, `clear_coat`, ...) -> chave -> cache de programa (Ogre-Next `Hlms::createShaderCacheEntry`; Filament `MaterialVariants`).
- **Offline vs runtime:** compilação offline para um pacote de shaders (rápido no arranque, necessário em Android), e modo de desenvolvimento com recompilação a quente.
- **Dependências:** glslang, SPIRV-Cross, SPIRV-Reflect, spirv-tools (todas já vendorizadas em `tmp/filament/third_party`).

## 5. Render (src/render)

**FrameGraph:** passes declaram recursos que leem e escrevem; o grafo aloca texturas transitórias e elimina passes não usados. Origem: Filament `filament/src/fg/*`, `details/Renderer.cpp:763-1652` (ordem dos passes).

**Ordem de passes (v1):**
1. Sombras (CSM direcional, depois spot/point) — Filament `ShadowMapManager.cpp`, `surface_shadowing.fs`
2. Depth pre-pass / structure (para SSAO e contacto)
3. Luzes: culling clustered/froxel — Filament `Froxelizer.cpp` (CPU) ou compute em GL 4.6 e Vulkan
4. Color pass PBR forward em `RGBA16F`/`R11G11B10F` — Filament `surface_brdf.fs`, `surface_shading_lit.fs`, `surface_light_indirect.fs`, `surface_light_punctual.fs`
5. Fog — Ogre-Next `800.PixelShader_piece_ps.any:925-934` (lâmpadas atravessam a névoa) e Filament `surface_fog.fs`
6. Bloom — Filament `materials/bloom/*`
7. Tonemap + dithering (+ color grading) — Filament `ToneMapper.h`, `colorGrading`
8. AA (FXAA/TAA) — Filament `materials/antiAliasing/*`
9. SSAO e refração (depois) — Filament `materials/ssao/*`, `RendererUtils.cpp:312-380`

**IBL:** DFG LUT, SH de 3 bandas, specular prefiltrado em cubemap, gerados por passes (e offline com a ferramenta). Origem: Filament `libs/ibl`, `tools/cmgen`, `surface_light_indirect.fs`.

**Luzes:** direcional, ponto, spot; unidades físicas (lux, lumen, candela) e exposição de câmara — Filament `Exposure.cpp`, `LightManager.h`; perfis IES para faróis — Ogre-Next `Components/Hlms/Pbs/src/LightProfiles/*`.

## 6. Material e cena (src/scene)

- **Material:** modelo PBR único com variantes: base color, normal, metal/rough, AO, emissivo; extensões: clear coat (tinta de carro), anisotropia, sheen/cloth, transmissão, detail maps.
- **Presets por defeito** (o pedido original): `Opaque`, `Metal`, `CarPaint`, `Glass`, `Emissive`, `Cloth`, `Unlit`, `Decal`, etc., criados de uma chamada.
- **Environment:** IBL + sol + fog + exposição.
- **Entrada de geometria:** API de vertex/index buffers para os loaders do utilizador.

## 7. Plataforma (src/platform)

SDL2 para Linux/Windows (janela, input, X11 e Wayland), `ANativeWindow` no Android. Cria o contexto/superfície e passa-o à RHI. Fica fora da RHI para o núcleo ser headless-capaz (modo sem janela para testes e servidores).

## 8. Estrutura de pastas

```
prisma/
  src/
    platform/   janela e input
    rhi/        interface + gl/ + gles/ + vulkan/
    shader/     pré-processador, glslang, spirv-cross, cache
    render/     framegraph + passes
    scene/      câmara, luzes, materiais, ambiente
  shaders/      fontes GLSL (portadas, com origem em comentário)
  assets/       HDRs, modelos de teste
  apps/         triangle, pbr_sphere, material_gallery, night_street
  tools/        bake de IBL, compilação de shaders
  docs/
```

## 9. Marcos (cada um termina com imagem a correr)

1. Triângulo GL 4.6 (feito).
2. RHI com texturas, UBO, depth, MRT e alvos HDR; sistema de shaders com glslang.
3. Esfera PBR + IBL; galeria de materiais.
4. CSM + bloom + tonemap.
5. Luzes clustered + fog + IES: cena da rua de noite.
6. Backend Vulkan com o mesmo conjunto de apps.
7. Backend GLES 3.1 e Android.
8. Extras: SSAO, AA, refração, mais materiais.

## 10. Riscos e como reduzir
- **SPIRV-Cross -> ES:** limites de ES 3.1 (sem cube array, derivadas por extensão). Reduzir mantendo um subconjunto GLSL comum e testando ES desde o marco 3 com Mesa.
- **Froxel no CPU:** é a parte mais difícil. Plano B: cluster por compute em GL 4.6 e Vulkan.
- **Float render targets em ES:** testar capability e ter fallback `R11G11B10F`.

## 11. Uso da lib `ct` (sem `std`)

| Necessidade | `ct` |
|---|---|
| Armazenamento interno dos recursos de cada manager (índices estáveis) | `SlotMap` |
| Listas (comandos, draw items, luzes) | `Vector`, `Array`, `Span` |
| Nomes, caminhos, fontes de shader | `String`, `StringView` |
| Caches de shader e de pipelines, tabelas de variantes | `HashMap` / `HashSet` (`hash_combine` para a chave de variante) |
| Memória por frame (render graph, listas temporárias) | `Arena` (reset por frame) |
| Objetos de tamanho fixo (nós do grafo, draw calls) | `Pool` |
| Callbacks (passes do frame graph, eventos) | `Function` |
| Posse partilhada onde for mesmo preciso (texturas do ambiente) | `Rc`, `Unique`, `Weak` |
| Compilação de shaders em paralelo, culling de luzes | `Thread`, `ThreadPool` (`parallel_for`) |
| Atlas de sombras | `RectPacker` |
| Ficheiros, packs de shaders, meshes | `Stream`, `FileStream`, `BinaryReader/Writer` |
| Config de cena e materiais | `Json`, `Ini` |
| Ordenação de draw calls | `sort.hpp` (`radix_sort`, `intro_sort`) |

Limites a ter em conta: C++14 (sem `if constexpr`, structured bindings, `std::optional`); sem exceções; os restos do esqueleto atual (que usa `std::unique_ptr`, `std::string`, `std::unordered_map`) têm de ser reescritos com `ct` quando se voltar a escrever código.
