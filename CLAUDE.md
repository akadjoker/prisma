# Regras do projeto (prisma: RHI + render novos)

Alvo: render moderno e realista (luzes dinâmicas, fog, bloom/HDR, sombras, IBL, materiais PBR variados). RHI e render novos de raiz, em C++17. Desktop: OpenGL 4.6 e Vulkan; mobile/web: OpenGL ES 3.1 e Android com Vulkan.

## Regra principal: NÃO inventar, reaproveitar
- Toda a técnica (BRDF, IBL, sombras, luzes, fog, bloom, tonemap, materiais) é **adaptada de código existente** nas referências, nunca desenhada de memória.
- Referências (só leitura): `/media/projectos/projects/cpp/engines/ogre/tmp/filament` (Apache-2.0) e `/media/projectos/projects/cpp/engines/ogre/tmp/Ogre-Next` (MIT).
- Para cada técnica: indicar ficheiro e linhas de origem no comentário do código. Se não existir nas referências, dizer e perguntar.
- Respeitar licenças e manter avisos de copyright.
- Não apagar pastas de referência nem media sem o utilizador decidir.
