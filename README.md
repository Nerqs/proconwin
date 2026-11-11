# Protótipo do Pro Controller (etapa 2)

App de console que conecta no Pro Controller do Switch 1 por USB ou Bluetooth, mostra todas as entradas já calibradas e testa o rumble HD e os LEDs. O objetivo é provar que entendemos o controle antes de criar o controle virtual (etapa 3).

## O que você precisa

- Windows 10 ou 11
- [Visual Studio 2022 Community](https://visualstudio.microsoft.com/pt-br/vs/community/) com a carga de trabalho **"Desenvolvimento para desktop com C++"** (já traz o CMake)
- [Git](https://git-scm.com/download/win), porque o CMake baixa o hidapi na primeira compilação
- Um Pro Controller do Switch 1 e um cabo USB-C com dados (não só de carga)

## Compilar

Abra o **"Developer PowerShell for VS 2022"** na pasta do projeto e rode:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

O executável fica em `build\Release\procon_proto.exe`.

## Testar

**Feche a Steam antes** (inclusive na bandeja do sistema). Se ela estiver aberta, ela também configura o controle e os dois brigam.

1. **USB:** ligue o controle pelo cabo e rode `build\Release\procon_proto.exe`.
2. **Bluetooth:** em Configurações > Bluetooth, adicione um dispositivo, segure o botão pequeno de sincronização no topo do controle até os LEDs correrem e escolha "Pro Controller". Depois rode o programa.

Durante o teste:

| Ação | Resultado esperado |
| --- | --- |
| Apertar qualquer botão | Aparece na linha "Botões" |
| Soltar os analógicos | Ficam em 0,00 |
| Analógicos até o fim | Chegam perto de ±1,00 |
| Controle parado na mesa | Giroscópio perto de 0 °/s, acelerômetro com cerca de 1 G em um dos eixos |
| Segurar A / B / X | Vibração grave / aguda / forte |
| Direcional | Muda qual LED de jogador está aceso |
| `+` e `-` juntos | Sai do programa e para a vibração |

## O que me mandar depois do teste

Copie e cole a saída do programa (as primeiras linhas e uma "foto" da tela durante o teste), uma vez por USB e uma por Bluetooth. Em especial:

- Os números de **relatórios/s** e **perdidos** em cada conexão
- Os valores do giroscópio com o controle parado (mostra o drift)
- Se algum botão não aparece ou aparece trocado
- Se o rumble funcionou nos três botões
- Qualquer mensagem de erro ou aviso

## Arquivos

- `src/protocol.*`: formatos dos relatórios, calibração, rumble HD (sem acesso ao hardware, fácil de testar)
- `src/controller.*`: conexão via hidapi, handshake USB, subcomandos, leitura da memória SPI
- `src/main.cpp`: o loop de teste no console

## Limitações conhecidas (ficam para as próximas etapas)

- Sem reconexão automática: se o controle desconectar, o programa fecha.
- Sem recalibração automática do drift do giroscópio.
- Zona morta fixa em 8%.
- NFC ainda não implementado.
- Só o primeiro Pro Controller encontrado é usado.
