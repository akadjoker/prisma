# Regras do projeto (RHI + render novos)

Alvo: render moderno e bonito (luzes dinâmicas clustered, fog, bloom/HDR, sombras, IBL, galeria de materiais PBR variados). Decisão do utilizador: **NÃO usar a biblioteca `GPU`** como base; criar uma RHI e um render novos de raiz (projeto grande). A `GPU` fica só como referência de leitura (ex.: demos de sombras e a lista de lacunas). Plataformas: Linux, Android e web.

## Regra principal: NÃO inventar, reaproveitar
- Toda a técnica (BRDF, IBL, sombras, luzes clustered, fog, bloom, tonemap, materiais) tem de ser **adaptada de código existente** nas referências, nunca desenhada de memória ou "à imaginação".
- Referências (só leitura): `tmp/filament`, `tmp/Ogre-Next`, `/media/projectos/projects/cpp/GPU` (antiga RHI do utilizador e demos, só leitura).
- Para cada técnica: citar **ficheiro e linhas** da referência de onde vem, o que foi mudado para a nossa API e porquê. Se não existir na referência, dizer "não existe" em vez de inventar; propor e perguntar.
- Preferir copiar/portar o shader e a lógica originais (respeitando licenças: Filament Apache-2.0, Ogre-Next MIT; manter avisos de copyright) e só mudar o necessário para a nossa nova RHI.
- Se a referência e a nossa RHI divergirem (ex.: falta uma capability), registar a lacuna na tabela de requisitos da RHI em vez de contornar com uma solução inventada.
- Em dúvida entre duas referências, mostrar as duas e perguntar qual seguir.

## Segurança
- Não apagar `Samples/Media`, shaders, materiais nem as pastas de referência sem o utilizador decidir.
- Não fazer push/commit sem pedido; o repo OGRE 14 antigo não se mexe.

## Decisões fixas
- C++17; backend v1 = GL 3.3 + GLES 3.0/WebGL2; forward + froxel no CPU; sem compute. Plano: `/home/djoker/.claude/plans/render-novo-rhi-pbr.md`.
