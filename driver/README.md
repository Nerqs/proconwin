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

1. Baixe o pacote `proconwin-windows-x64` na aba **Actions** do GitHub (abra a última execução verde e role até "Artifacts") e extraia numa pasta.
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
4. Conecte o Pro Controller e abra o **`procon_tray.exe`**. Ele fica na bandeja, ao lado do relógio (pode estar escondido na setinha ^). Clique no ícone para ver o estado do controle e a bateria, e para mudar:
   - giroscópio (desligado, mouse ou analógico direito) e recalibrar;
   - layout dos botões (pela posição ou pelo nome);
   - "Estou usando a Steam" e "Pausar sozinho com jogos da Steam";
   - luz do botão HOME;
   - iniciar com o Windows.

   As escolhas ficam salvas em `%APPDATA%\ProconWin\config.ini`.

   Também existe a versão de console, para testes:
   ```powershell
   .\procon_virtual.exe
   ```
   Opções: `--gyro mouse` (giroscópio move o mouse), `--gyro stick` (giroscópio soma no analógico direito), `--sens N` (sensibilidade), `--layout nome` (A da Nintendo = A do Xbox), `--luz-home off` (apaga a luz do botão HOME; a tecla **L** acende e apaga com o programa aberto).

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
