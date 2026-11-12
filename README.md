# ProconWin

Suporte nativo ao Pro Controller do Switch no Windows: o controle aparece como um controle de Xbox 360, com giroscópio, vibração, perfis por jogo e um modo para usar o PC (mouse e teclado), sem modo de teste, sem desligar o Secure Boot e sem atrapalhar o Steam Input.

## Controles

| Controle | Estado |
| --- | --- |
| Pro Controller (Switch 1), USB e Bluetooth | ✅ Pronto (versão 1.0) |
| Joy-Con (Switch 1), sozinhos ou em par | Planejado |
| Pro Controller e Joy-Con do Switch 2 | Planejado |
| Controles do Nintendo Switch Online (SNES, N64, Mega Drive...) | Planejado |

Um controle de cada vez: o próximo só começa quando o anterior está 100%.

## O que o ProconWin faz

- Controle de Xbox 360 virtual (driver em modo usuário, UMDF2): funciona em qualquer jogo com XInput
- Botões pela posição (padrão) ou pelo nome, editor de botões clicando no desenho do controle, e perfis por jogo
- Giroscópio como mouse ou como analógico direito, com sensibilidade de 1 a 10
- Modo computador: analógico direito = mouse, ZL/ZR = cliques, outros botões = teclas (liga segurando Captura 1 s)
- Pausa sozinho quando um jogo da Steam abre (o Steam Input assume), ou pelo botão "Estou usando a Steam"
- Reconexão automática, bateria, luz do HOME, vibração liga/desliga, segurar HOME 3 s desliga o controle
- Avisa quando sai uma versão nova e atualiza com um clique

## Instalar

1. Baixe o **`ProconWin-Setup-x.y.z.exe`** da [última versão](https://github.com/Nerqs/proconwin/releases/latest).
2. Abra e siga o instalador. Ele pede permissão de administrador para instalar o driver do controle virtual. O Windows pode mostrar "O Windows protegeu o computador" porque o instalador não tem uma assinatura paga; clique em **Mais informações** e **Executar assim mesmo**.
3. Conecte o Pro Controller (cabo USB-C ou Bluetooth) e abra o **ProconWin** pelo menu Iniciar.

O driver é instalado com um certificado criado só para o seu PC. A chave privada dele é apagada logo depois de assinar o driver, então esse certificado não serve para assinar mais nada. O desinstalador (em Configurações > Aplicativos) remove o driver e o certificado.

**Atualizar:** quando sair uma versão nova, o ProconWin avisa ao lado do relógio e mostra uma faixa azul na janela. Clique em **Atualizar agora**: ele baixa o instalador, confere o arquivo (SHA-256) e atualiza sozinho, mantendo as configurações e os perfis. Dá para desligar o aviso nas opções.

Como usar cada opção, Steam, modo computador e problemas comuns: veja [driver/README.md](driver/README.md).

> O ProconWin é um projeto independente, **sem nenhuma ligação com a Nintendo**, a Microsoft ou a Valve. Nintendo Switch, Joy-Con e Pro Controller são marcas da Nintendo; Xbox é marca da Microsoft; Steam é marca da Valve. Elas aparecem aqui só para dizer com o que o programa funciona.

## Estrutura

| Pasta | O que tem |
| --- | --- |
| `core/` | Núcleo: protocolo do controle, mapeamento para Xbox, giroscópio, Steam, modo computador, configurações e atualização |
| `driver/` | Driver do controle virtual (`ProconWinXusb`, modo usuário) e os scripts de instalação |
| `tools/engine/` | Motor: Pro Controller para o controle virtual, reconexão e pausa da Steam |
| `tools/procon_tray/` | O app (janela e bandeja) |
| `tools/procon_virtual/` | Versão de console do app |
| `tools/procon_proto/` | Protótipo de console para testar o controle |
| `installer/` | Instalador (Inno Setup) |
| `tests/` | Testes do núcleo, rodam sem o controle conectado |

## Compilar (para quem quer mexer no código)

### O que você precisa

- Windows 10 ou 11
- [Visual Studio 2022 Community](https://visualstudio.microsoft.com/pt-br/vs/community/) com a carga de trabalho **"Desenvolvimento para desktop com C++"** (já traz o CMake)
- [Git](https://git-scm.com/download/win), porque o CMake baixa o hidapi na primeira compilação
- Um Pro Controller do Switch 1 e um cabo USB-C com dados (não só de carga)

### Compilar e testar

No **"Developer PowerShell for VS 2022"**, na pasta do projeto:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

O protótipo fica em `build\Release\procon_proto.exe`. **Feche a Steam antes** de rodar (inclusive na bandeja do sistema).

Para conectar por Bluetooth: Configurações > Bluetooth > adicionar dispositivo, segure o botão de sincronização no topo do controle até os LEDs correrem e escolha "Pro Controller".

| Ação | Resultado esperado |
| --- | --- |
| Apertar qualquer botão | Aparece em "Botões" e o equivalente em "Xbox" (B da Nintendo = A do Xbox) |
| Soltar os analógicos | Ficam em 0,00 |
| Controle parado por ~1 s | "Drift" mostra "ok" e o giroscópio fica perto de 0,0 |
| Apertar Captura | Recalibra o drift na hora (deixe o controle parado) |
| Girar o controle para a direita | "Mouse acumulado" x sobe |
| Inclinar a frente do controle para cima | "Mouse acumulado" y desce |
| Segurar A / B / X | Vibração grave / aguda / forte |
| Direcional | Muda o LED do jogador |
| `+` e `-` juntos | Sai do programa |

## Plano

1. ~~Referência do protocolo~~
2. ~~Protótipo de leitura~~
3. ~~Controle virtual de Xbox 360 + convivência com a Steam~~
4. ~~App, editor de botões, perfis, reconexão, instalador com atualização~~ (Pro Controller 1.0)
5. Próximos controles da Nintendo (veja a tabela lá em cima)

## Publicar uma versão nova

1. Troque a versão em `project(proconwin VERSION x.y.z)` no `CMakeLists.txt`.
2. Depois do merge no `main`, crie a tag: `git tag vx.y.z && git push origin vx.y.z`.
3. O GitHub Actions compila, monta o instalador e publica a versão. Os apps instalados avisam sozinhos.

## Licença

[MIT](LICENSE). Código de terceiros e as licenças deles: [third_party/README.md](third_party/README.md).
