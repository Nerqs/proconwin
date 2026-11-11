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
4. Feche a Steam, conecte o Pro Controller e rode:
   ```powershell
   .\procon_virtual.exe
   ```
   Opções: `--gyro mouse` (giroscópio move o mouse), `--gyro stick` (giroscópio soma no analógico direito), `--sens N` (sensibilidade), `--layout nome` (A da Nintendo = A do Xbox).
5. Teste em `joy.cpl` (Win+R) ou num jogo. No Gerenciador de Dispositivos, o driver aparece em **Dispositivos do sistema** como "ProconWin - controle de Xbox 360 virtual".

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
