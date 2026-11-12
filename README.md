# ProconWin

Suporte nativo ao Pro Controller do Switch no Windows: o controle aparece como um controle de Xbox 360, com giroscópio, vibração, perfis por jogo e um modo para usar o PC (mouse e teclado), sem modo de teste, sem desligar o Secure Boot e sem atrapalhar o Steam Input.

**Estado atual:** o Pro Controller do Switch 1 funciona por USB e Bluetooth. Falta o instalador. Outros controles da Nintendo vêm depois.

- Controle de Xbox 360 virtual (driver em modo usuário, UMDF2), funciona em qualquer jogo com XInput
- Botões pela posição (padrão) ou pelo nome, editor de botões e perfis
- Giroscópio como mouse ou como analógico direito, com sensibilidade de 1 a 10
- Modo computador: analógico direito = mouse, ZL/ZR = cliques, outros botões = teclas
- Pausa sozinho quando um jogo da Steam abre (o Steam Input assume), ou pelo botão "Estou usando a Steam"
- Reconexão automática, bateria, luz do HOME, vibração liga/desliga, segurar HOME 3 s desliga o controle

Como instalar e usar: veja [driver/README.md](driver/README.md).

> O ProconWin é um projeto independente, **sem nenhuma ligação com a Nintendo**, a Microsoft ou a Valve. Nintendo Switch e Pro Controller são marcas da Nintendo; Xbox é marca da Microsoft; Steam é marca da Valve. Elas aparecem aqui só para dizer com o que o programa funciona.

## Estrutura

| Pasta | O que tem |
| --- | --- |
| `core/` | Núcleo: protocolo do controle, mapeamento para Xbox, giroscópio (drift, modo mouse, modo analógico) e acesso ao controle via hidapi |
| `tools/procon_proto/` | Protótipo de console para testar o controle |
| `tests/` | Testes do núcleo, rodam sem o controle conectado |

## O que você precisa

- Windows 10 ou 11
- [Visual Studio 2022 Community](https://visualstudio.microsoft.com/pt-br/vs/community/) com a carga de trabalho **"Desenvolvimento para desktop com C++"** (já traz o CMake)
- [Git](https://git-scm.com/download/win), porque o CMake baixa o hidapi na primeira compilação
- Um Pro Controller do Switch 1 e um cabo USB-C com dados (não só de carga)

## Compilar e testar

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
4. App na bandeja, editor de botões, reconexão (feito); instalador com atualização (em andamento)
5. Outros controles da Nintendo

## Licença

[MIT](LICENSE). Código de terceiros e as licenças deles: [third_party/README.md](third_party/README.md).
