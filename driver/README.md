# Driver do controle virtual

O Pro Controller aparece no Windows como um **controle de Xbox 360** graças ao driver `ProconWinXusb`, que roda em **modo usuário** (UMDF2).

## Por que modo usuário

O Windows só exige a assinatura da Microsoft (ou o modo de teste, que exige o Secure Boot desligado) para drivers que rodam no **kernel**. Um driver UMDF roda como um programa comum, dentro do `WUDFHost.exe` do próprio Windows, então:

- **não precisa de modo de teste**;
- **não precisa desligar o Secure Boot**;
- se der um erro, o pior que acontece é o controle virtual sumir, sem tela azul.

O pacote precisa de uma assinatura que o PC confie. O `instalar.ps1` cria um certificado que só existe no seu PC, assina o pacote com ele e depois apaga a chave privada, para que ninguém consiga usar esse certificado para assinar outra coisa.

Como o XInput enxerga o controle: o driver publica a mesma interface do `xusb22.sys` (o driver dos controles de Xbox 360 com fio) e responde aos pedidos do XInput com o estado que o `procon_virtual` manda. Esse protocolo não é documentado pela Microsoft; os formatos vêm de projetos que reimplementam o XInput (usados só como referência de leitura). O controle virtual só aparece enquanto o `procon_virtual` está rodando.

## Instalar

O jeito fácil é o instalador (veja o [README principal](../README.md#instalar)): ele faz os passos abaixo sozinho. Para instalar à mão, a partir do zip:

1. Baixe o pacote [proconwin-windows-x64.zip](https://github.com/Nerqs/proconwin/releases/download/ultima-versao/proconwin-windows-x64.zip) (versão de teste, atualizada a cada mudança) e extraia numa pasta.
2. Se você instalou o driver antigo (`ProconWinBus`) e ligou o modo de teste, remova antes:
   ```powershell
   pnputil /enum-drivers   # ache o oemNN.inf do ProconWinBus
   pnputil /delete-driver oemNN.inf /uninstall /force
   bcdedit /set testsigning off
   ```
3. Abra o **PowerShell como Administrador** na pasta do pacote e rode:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\instalar.ps1
   ```
4. Conecte o Pro Controller e abra o **`procon_tray.exe`**. Abre a janela do ProconWin, com tudo num lugar só:
   - em cima, o estado do controle e a bateria;
   - à esquerda, o desenho de um controle: o botão que você aperta acende em azul, os analógicos se mexem junto, e clicando num botão do desenho você escolhe a função dele. Em cima do desenho escolha **Nos jogos (Xbox)** ou **Modo computador** para editar um ou outro. Contorno laranja = botão fora do padrão;
   - à direita, o perfil em uso, **Novo perfil** (nome do jogo, começando dos botões atuais ou de um padrão), giroscópio e sensibilidade, e as opções com interruptores;
   - embaixo, **Diagnóstico** (atualiza sozinho), a pasta das configurações, **Fechar** e **Sair**.

   Fechar a janela deixa o programa rodando na bandeja, ao lado do relógio (pode estar escondido na setinha ^). Clique no ícone para abrir a janela de novo, ou com o botão direito para o menu rápido. **Sair** fecha de vez. Com "Iniciar com o Windows", ele abre só na bandeja.

   Opções da janela e do menu:
   - giroscópio (desligado, mouse ou analógico direito), a sensibilidade (1 a 10, cada modo tem a sua) e recalibrar;
   - botões: pela posição, pelo nome, ou do seu jeito no desenho;
   - perfil em uso (um por jogo, por exemplo);
   - modo computador (mouse e teclado) e a velocidade do mouse;
   - "Estou usando a Steam" e "Pausar sozinho com jogos da Steam";
   - vibração (liga e desliga);
   - luz do botão HOME;
   - iniciar com o Windows.

   As escolhas ficam salvas em `%APPDATA%\ProconWin\config.ini`.

   **Editor de botões:** clique num botão do desenho e escolha para qual botão do Xbox ele vai (ou "nada"). A mudança vale na hora. Passe o mouse por cima para ver a função atual. Os botões "Padrão: pela posição" e "Padrão: pelo nome" voltam ao começo.

   **Modo computador:** para usar o Windows com o controle, sem mouse. Ligue pelo menu **Modo computador** da bandeja ou **segurando Captura por 1 segundo** no controle (ele vibra para confirmar). Segurar de novo desliga. Enquanto está ligado, o controle de Xbox virtual fica parado, então os jogos não recebem nada em dobro.

   | Controle | Faz |
   |---|---|
   | Analógico direito | move o mouse |
   | Analógico esquerdo | rola a página (para os lados também) |
   | ZL / ZR | clique esquerdo / clique direito |
   | R3 (apertar o analógico direito) | clique do meio |
   | A / B | Enter / Esc |
   | X / Y | Apagar / Espaço |
   | Direcional | setas |
   | L / R | voltar / avançar (navegador) |
   | + / − | menu Iniciar / ver todas as janelas |
   | L3 (apertar o analógico esquerdo) | teclado virtual |

   Tudo isso dá para trocar no editor, em "Editar: Modo computador" (tem Alt+Tab, fechar janela, volume, play/pausa, recortar a tela e mais). O giroscópio continua funcionando como mouse se estiver ligado. Limite do Windows: programas abertos como administrador não aceitam cliques e teclas do controle.

   **Perfis:** um perfil guarda os botões e o giroscópio. Para criar, ajuste do jeito que quer, digite um nome (o do jogo, por exemplo) e clique em "Criar com os botões atuais". Enquanto um perfil está em uso, toda mudança vai direto para ele. Troque de perfil pelo editor ou pelo menu **Perfil** da bandeja. Cada perfil é um arquivo em `%APPDATA%\ProconWin\perfis\`.

   Também existe a versão de console, para testes:
   ```powershell
   .\procon_virtual.exe
   ```
   Opções: `--gyro mouse` (giroscópio move o mouse), `--gyro stick` (giroscópio soma no analógico direito), `--sens N` (sensibilidade), `--layout nome` (A da Nintendo = A do Xbox), `--luz-home off` (apaga a luz do botão HOME; a tecla **L** acende e apaga com o programa aberto), `--modo-pc` (começa no modo computador; a tecla **P** liga e desliga). As teclas **+** e **-** mudam a sensibilidade do giroscópio. A tecla **V** liga e desliga a vibração (ou `--sem-vibracao`).

   **Steam:** com a Steam aberta você não precisa fechar mais nada. Quando um jogo aberto pela Steam está rodando, o `procon_virtual` pausa sozinho e deixa o Steam Input usar o controle, sem entrada dobrada. Quando o jogo fecha, ele volta. A tecla **S** liga o modo "Estou usando a Steam" (fica pausado o tempo todo, por exemplo no Big Picture). Também dá para escolher na linha de comando: `--steam auto` (padrão), `--steam sempre` ou `--steam nunca`.

   Se o controle desconectar ou for desligado (segure HOME por 3 s no Bluetooth), o programa espera ele voltar.
5. Teste num jogo ou em https://hardwaretester.com/gamepad (Chrome/Edge). O `joy.cpl` não mostra este controle, porque ele usa DirectInput/HID e o driver só fala XInput. No Gerenciador de Dispositivos, o driver aparece em **Dispositivos do sistema** como "ProconWin - controle de Xbox 360 virtual".

## Remover

```powershell
powershell -ExecutionPolicy Bypass -File .\desinstalar.ps1
```

## Compilar

Precisa do Visual Studio 2022 com o [WDK](https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk):

```powershell
msbuild driver\ProconWinXusb\ProconWinXusb.vcxproj /p:Configuration=Release /p:Platform=x64
```

O resultado fica em `driver\bin\xusb\Release\`. O catálogo (`.cat`) é gerado com `Inf2Cat /driver:<pasta> /os:10_X64`. A CI do GitHub faz tudo isso e publica o pacote pronto.

## Driver de kernel (guardado)

A pasta `ProconWinBus/` tem o driver de kernel baseado no ViGEmBus. Ele só funciona com modo de teste ou com assinatura da Microsoft, então não entra no pacote. Fica no repositório para quando a assinatura for decidida.
