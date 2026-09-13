# Database, busca e status — 12/09/2026

Revisão da interface 1.2.2 após o feedback sobre alinhamento dos status, largura da busca, crash ao ocultar Support me e organização do banco. Substitui a organização do gerenciador descrita no [relatório anterior](ui-layout-fixes-validation.md).

- **Status:** texto e ícone centralizados verticalmente no badge, com altura mínima consistente e limites tipográficos ajustados.
- **Busca:** campos de Running e Installed crescem com a largura da toolbar, mantendo as ações à direita.
- **Support me:** ocultar a página não acessa mais o controle de uma página que ainda não foi criada. A preferência é mantida ao fechar e reabrir a UI.
- **Database:** página própria na sidebar, criada no primeiro acesso. Caminhos, scan e manutenção ficam na mesma página, sem modal nem abas internas. O botão redundante de gerenciamento saiu da toolbar de Installed. Caminhos salvos, texto em edição e rolagem sobrevivem à navegação. Inglês e PT-BR incluídos.

## Verificação

Build Release x64 concluído. Oito cenários funcionais de UI e três cenários de reabertura/redimensionamento passaram, usando `Tests/UiRedesignFakeHost.py`, perfil e pipe exclusivos. A sessão principal de áudio não foi alterada.

Cobertura: ocultar Support antes do primeiro acesso, mostrar/ocultar depois do acesso, fechar/reabrir com a preferência salva, acessar Database antes e depois de Plugins, persistência de caminho Unicode, rejeição de duplicata, navegação com texto em edição, scan/cancelamento, remoção de ausentes e cancelamento da confirmação de limpeza. Foram conferidos toolbar, estados dos plugins, agrupamento, Dashboard, Diagnostics, Preferred device, inglês/PT-BR e temas claro/escuro.

A primeira execução do teste de Preferred device tentou medir o título fora da área visível, que a UI Automation informa com dimensões zero. O teste foi corrigido para colocar o título em vista; a nova execução passou em Compact e Expanded. As capturas confirmam a altura limitada do card.

- [Execução principal](../out/ui-database-20260912/visual/screenshots/results.json)
- [Preferred device após correção da medição](../out/ui-database-20260912/visual/preferred/results.json)
- [Reabertura e janela menor](../out/ui-database-20260912/visual/reopen/results.json)

Os 18 testes CTest passaram em **12,23 segundos**. Não foram repetidos ensaios prolongados de hardware nem instalação do MSI nesta revisão de interface.

## Inspeção visual

Capturas examinadas da UI nativa em DPI de 200%, incluindo janela de 1700 pixels físicos:

| Área | Captura |
| --- | --- |
| Status e busca em Installed | [Lista plana](../out/ui-database-20260912/visual/screenshots/installed-flat.png) |
| Running e bypass | [Toolbar](../out/ui-database-20260912/visual/screenshots/running-toolbar.png), [bypass](../out/ui-database-20260912/visual/screenshots/running-bypassed-badge.png) |
| Fabricantes e hover | [Grupos](../out/ui-database-20260912/visual/screenshots/installed-manufacturer-hover.png) |
| Database | [Caminhos](../out/ui-database-20260912/visual/screenshots/database-paths.png), [scan e manutenção](../out/ui-database-20260912/visual/reopen/database-narrow-bottom.png) |
| Database em PT-BR/claro | [Página localizada](../out/ui-database-20260912/visual/screenshots/database-portuguese-light.png) |
| Janela menor | [Running](../out/ui-database-20260912/visual/reopen/running-narrow.png), [Installed](../out/ui-database-20260912/visual/reopen/installed-narrow.png) |
| Support oculto após reabrir | [Settings](../out/ui-database-20260912/visual/reopen/support-hidden-after-reopen.png) |
| Preferred device | [Compact](../out/ui-database-20260912/visual/preferred/preferred-device-compact-long-name.png) |

## Pacotes

[ZIP portátil](../out/release-ui-database/LightHostModern-Portable.zip) e [MSI](../out/release-ui-database/LightHostModern-Setup.msi), versão 1.2.2. Host, UI, scanner e auxiliar de atualização presentes; fixtures excluídas. O executável, `App.xbf`, `MainWindow.xbf`, `DatabasePageView.xbf`, `PluginsPageView.xbf`, `SettingsPageView.xbf` e `resources.pri` do ZIP correspondem por SHA-256 ao build e ao staging. A tabela de arquivos do MSI contém os componentes e layouts esperados.

[Verificação dos pacotes](../out/release-ui-database/ui-database-verification.json).
