# Dashboard, Plugins e Diagnostics — 12/09/2026

Os ajustes posteriores de Preferred device, abas, hover, toolbar e posição dos status estão no [relatório de correções de layout](ui-layout-fixes-validation.md).

Esta revisão substitui o desenho anterior dessas três áreas, mantendo C++/WinRT, WinUI 3 e a versão 1.2.2.

- Dashboard: removidos mute e bypass. Restaurados os medidores da tag `v1.2.2`: 28 segmentos de 7 × 26 DIPs, espaçamento de 4 DIPs, amplitude de pico linear e as cores originais verde/amarelo/vermelho. Sem textos RMS/Hold ou menu de canais/clipping.
- Plugins: toolbars nativas `CommandBar` para mute, bypass, ordenação e agrupamento. Ações do banco concentradas em **Manage plugin database**, com abas de caminhos, scan e manutenção. Removidas as ações de scan redundantes da página.
- Fabricantes: cabeçalhos em cards próprios, plugins em cards separados com recuo e linhas de conexão. Mantidos IDs estáveis, atualização incremental, pesquisa e virtualização.
- Estados: badges com ícones e cores para ativo/em execução, bypass, disponível e erro/indisponível, usando recursos de tema.
- Diagnostics: ícone, título e descrição no topo; valores sempre abaixo.
- Interações: um clique em mute/bypass durante a atualização estrutural aguarda o término da leitura antes do primeiro envio. A troca de idioma preserva os bindings dos controles; nomes das abas e labels das toolbars atualizam em português e inglês.
- Modal: caminhos Unicode são salvos imediatamente, duplicatas são rejeitadas e os botões de procurar, abrir e remover continuam disponíveis. A confirmação de limpeza abre após o fechamento do modal principal e retorna à aba Manutenção ao cancelar.

## Verificação

Build Release x64 concluído. Os 18 testes CTest passaram em 11,43 segundos. Os seis cenários de interface foram verificados por UI Automation e capturas; após as correções, foram repetidos os cenários afetados de gerenciamento do banco e idioma. A escrita concorrente do log do host simulado também foi serializada para evitar registros intercalados.

Os testes usaram perfis, pipes, preferências e dados simulados separados. Foram conferidos sinais de 0%, 25%, 50%, 80% e saturação em 100%, mute na saída, estados de bypass e erro, agrupamento e busca, as três abas do modal, scan/cancelamento, remoção de ausentes e cancelamento da confirmação de limpeza. Inglês/PT-BR e temas claro/escuro foram inspecionados visualmente no layout Compact a 200% de DPI.

Esta revisão não repetiu ensaios prolongados de hardware/desempenho nem executou o instalador. A validação do modal de scan usa transporte simulado; a regressão CTest cobre o scanner separadamente.

- [Resultados consolidados](../out/ui-toolbar-20260912/validation.json)
- [Verificação dos pacotes](../out/release-ui-toolbar/ui-toolbar-verification.json)
- [Cenários de interface](../WinUI/ui-tests-plugin-workspace.ps1)

## Capturas inspecionadas

| Tela | Captura |
| --- | --- |
| Dashboard com medidores originais | [Dashboard](../out/ui-toolbar-20260912/final/checks/dashboard-final.png) |
| Toolbar e agrupamento | [Plugins agrupados](../out/ui-toolbar-20260912/final/checks/grouped-final.png) |
| Bypass e erro | [Bypass](../out/ui-toolbar-20260912/verified/checks/running-bypassed-badge.png), [erro](../out/ui-toolbar-20260912/verified/checks/running-error-badge.png) |
| Busca dentro dos grupos | [Resultado filtrado](../out/ui-toolbar-20260912/verified/checks/installed-grouped-filtered.png) |
| Gerenciamento do banco | [Caminhos](../out/ui-toolbar-20260912/final/checks/database-paths.png), [scan](../out/ui-toolbar-20260912/final/checks/database-scan-active.png), [manutenção](../out/ui-toolbar-20260912/final/checks/database-maintenance.png) |
| Português e tema claro | [Plugins](../out/ui-toolbar-20260912/final/checks/installed-portuguese-light.png), [modal](../out/ui-toolbar-20260912/final/checks/database-portuguese-light.png) |
| Diagnostics com valores abaixo | [Diagnostics](../out/ui-toolbar-20260912/final/checks/diagnostics-final.png) |

## Pacotes locais

[ZIP portátil](../out/release-ui-toolbar/LightHostModern-Portable.zip) e [MSI](../out/release-ui-toolbar/LightHostModern-Setup.msi). Ambos incluem host, UI, scanner e auxiliar de atualização. A UI do ZIP corresponde por SHA-256 ao executável Release e ao staging. O MSI informa a versão 1.2.2. Fixtures e plugins de terceiros não foram incluídos. Os pacotes não foram publicados ou instalados.
