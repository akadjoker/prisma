# prisma — arquitetura dos backends

Estado: só desenho. Backends: **GL 4.6** e **GLES 3** (a implementar) e **Vulkan** (previsto na interface, sem implementação por agora).

## 1. Princípio

O render fala só com uma interface interna, o `Driver`, feita de recursos por handle e comandos explícitos (render pass, pipeline, descritores, draw, dispatch, barreiras). Cada backend é uma implementação do `Driver`. O render nunca inclui código de um backend, nem sabe qual está ativo. Origens: Godot `servers/rendering/rendering_device_driver.h` (interface do driver sobre `RenderingDevice`), Filament `filament/backend` (Driver + command stream).

A interface é desenhada para o Vulkan (o mais exigente). O GL traduz. Por isso o Vulkan, quando vier, não obriga a mudar o render.

## 2. Escolha do backend por plataforma (em tempo de compilação)

| Plataforma | Backend | Loader | Contexto |
|---|---|---|---|
| Linux / Windows (desktop) | GL 4.6 (Vulkan depois) | `opengl/OpenGL.h/.cpp` | SDL2 |
| Android | GLES 3 (Vulkan depois) | `opengl/OpenGLES3.h/.cpp` | EGL |

Motivo da escolha em compile-time: `OpenGL.h` e `OpenGLES3.h` declaram globais com o mesmo nome, por isso nunca coexistem no mesmo binário. Isto não limita nada: uma build é desktop ou mobile.

## 3. Estrutura de pastas

```
src/rhi/
  Driver            interface do backend (abstrata)
  Types             enums, formatos, descritores de recursos e de estado
  Caps              capabilities preenchidas pelo backend
  gl/
    GLDriver        lógica comum a GL e GLES (ver secção 4)
    GLState         cache de estado
    GLFormats       tabela formato RHI -> formato GL
    GLShaderCache   programas e variantes
    gl4/            ligação ao loader desktop + criação de contexto + extras 4.x
    gles3/          ligação ao loader ES + extras ES
  vulkan/
    (vazio por agora; só um VulkanDriver "não implementado" que falha ao criar)
opengl/             os teus loaders (OpenGL.*, OpenGLES3.*), não são alterados
```

## 4. O que o `GLDriver` partilha entre GL 4.6 e GLES 3

A maior parte do código é a mesma; muda só o que o ES não tem ou tem diferente.

| Tema | Comum | GL 4.6 | GLES 3 |
|---|---|---|---|
| Buffers | glBufferData/SubData, UBO, SSBO, indirect | DSA (`glCreateBuffers`), mapeamento persistente | sem DSA, mapeamento por range |
| Texturas | `glTexStorage*`, mips, samplers | DSA (`glCreateTextures`, `glTextureStorage*`) | `glTexStorage2D/3D`, formatos por extensão |
| Render targets | FBO, MRT, `glBlitFramebuffer` | `glClipControl`, resolve MSAA | `glInvalidateFramebuffer`; `EXT_color_buffer_float` para HDR |
| Compute | `glDispatchCompute`, `glMemoryBarrier` | sim | sim (ES 3.1+) |
| Estado | cache de blend/depth/raster/viewport | `glClipControl` (depth 0..1) | depth -1..1 (convenção a normalizar no shader) |
| Extensões | | `glExt::*` | `glESExt::*` |

**Convenção de profundidade:** o render usa depth 0..1 (reverse-Z). O GL 4.6 usa `glClipControl`; o ES não o tem, então o GLES compensa na matriz de projeção ou no shader. A compensação mora no backend.

## 5. A interface `Driver` (resumo do que faz, sem código)

- **Recursos por handle:** buffers (vertex, index, uniform, storage, indirect), texturas (2D, array, cube, 3D, com mips e amostras), samplers, programas de shader, pipelines (gráfico e compute), render targets, fences/queries. Os handles são índices com geração (`ct::SlotMap`), validados a cada uso.
- **Pipeline state objects:** o estado (blend, depth/stencil, raster, formato dos anexos, layout de vértices) vem num PSO imutável. O GL guarda-o e aplica só as diferenças ao estado atual (cache).
- **Descritores:** grupos de recursos (UBO, texturas, storage) ligados juntos, por set. O GL traduz para binding points e texture units através de uma tabela de bindings (Filament `BindingMap.h`, `GLDescriptorSet.*`); o Vulkan usa descriptor sets nativos.
- **Comandos:** begin/end de render pass (anexos, load/store/clear), bind de pipeline e descritores, draw (indexed, instanced, indirect), dispatch, copy/blit/resolve, barreiras. No GL as barreiras são traduzidas para `glMemoryBarrier`; as de imagem num render pass para `glInvalidateFramebuffer`.
- **Execução:** v1 imediata na thread de render. O desenho já separa "gravar" de "executar", para depois haver command buffers por thread sem mudar a interface.
- **Capabilities:** o backend preenche uma estrutura (compute, SSBO, MSAA máximo, formatos HDR renderáveis, cube array, aniso, tamanho máximo de UBO, convenção de depth). O render escolhe por capability e nunca pelo nome do backend.
- **Janela e contexto:** vêm da camada de plataforma (SDL2 no desktop, EGL no Android), que cria o contexto e o entrega ao backend. A RHI não conhece a janela.

## 6. Shaders por backend

O pacote de shaders (ver `ARCHITECTURE.md` secção 4) traz, por variante, SPIR-V, reflexão e GLSL gerado:

| Backend | Usa |
|---|---|
| GL 4.6 | GLSL 460 gerado pelo SPIRV-Cross (o `OpenGL.h` não tem `glSpecializeShader`; acrescentá-lo é um passo futuro opcional) |
| GLES 3 | GLSL ES gerado pelo SPIRV-Cross |
| Vulkan | SPIR-V direto |

O `GLShaderCache` compila o GLSL do pacote, mantém os programas por variante e liga as reflexões à tabela de bindings.

## 7. Vulkan (adiado)

Fica só reservado: o `Driver` já tem conceitos de render pass, descritores e barreiras. Quando se implementar, entra um `VulkanDriver` com alocador de memória, sincronização e swapchain; origens: Filament `backend/src/vulkan/*` e Godot `drivers/vulkan/*`. Nada no render muda.

## 8. Ordem de implementação

1. Interface `Driver`, tipos e capabilities.
2. `GLDriver` comum + backend GL 4.6 (recursos, PSO, render pass, draw) até o triângulo.
3. Texturas, UBO, descritores, depth e render targets HDR.
4. Compute e SSBO (luzes clustered).
5. Backend GLES 3 sobre o mesmo `GLDriver`.
6. Vulkan.

## 9. Pontos em aberto
- Se acrescentas `glSpecializeShader` ao `OpenGL.h` para usar SPIR-V direto no GL.
- Como lidar com `EXT_color_buffer_float` em dispositivos ES que não o têm (fallback `R11G11B10F`).
- Se o command buffer fica já em v1 ou só depois.
