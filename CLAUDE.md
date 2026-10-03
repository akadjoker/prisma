# Regras do projeto (prisma: RHI + render novos)

Alvo: render moderno e realista (luzes dinâmicas, fog, bloom/HDR, sombras, IBL, materiais PBR variados). RHI e render novos de raiz, em C++14 (o padrão do `ct`). Desktop: OpenGL 4.6 e Vulkan; mobile/web: OpenGL ES 3.1 e Android com Vulkan.

## Regra principal: NÃO inventar, reaproveitar
- Toda a técnica (BRDF, IBL, sombras, luzes, fog, bloom, tonemap, materiais) é **adaptada de código existente** nas referências, nunca desenhada de memória.
- Referências (só leitura): `/media/projectos/projects/cpp/engines/ogre/tmp/filament` (Apache-2.0) e `/media/projectos/projects/cpp/engines/ogre/tmp/Ogre-Next` (MIT).
- Para cada técnica: indicar ficheiro e linhas de origem no comentário do código. Se não existir nas referências, dizer e perguntar.
- Respeitar licenças e manter avisos de copyright.
- Não apagar pastas de referência nem media sem o utilizador decidir.

## Código: sem `std`, usar `ct`
- **Proibido** `std::vector`, `std::string`, `std::unordered_map`, `std::unique_ptr`, `std::function`, `std::thread`, etc. Usar a lib do utilizador `ct` (`/media/projectos/projects/cpp/containers`, https://github.com/akadjoker/containers): `ct::Vector`, `ct::String`, `ct::HashMap`, `ct::SlotMap`, `ct::Arena`, `ct::Pool`, `ct::Function`, `ct::Rc/Unique`, `ct::Thread/ThreadPool`, `ct::RectPacker`, `ct::BinaryReader`, `ct::Json`...
- Sem exceções (a `ct` aborta em uso inválido); sem RTTI.
- Se faltar um contentor na `ct`, perguntar ao utilizador em vez de usar `std` ou inventar.
- Estilo da `ct`: **sem comentários no código-fonte**. A origem das técnicas portadas (ficheiro:linhas do Filament/Ogre-Next) regista-se em `docs/PORTING.md`, não em comentários.
- **Só escrever código quando o utilizador pedir.** Planos e documentos não incluem código novo.
