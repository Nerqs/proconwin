# Driver do controle virtual (ProconWinBus)

O `ProconWinBus` é o driver que cria o controle de Xbox 360 virtual. Ele é baseado no [ViGEmBus](https://github.com/nefarius/ViGEmBus) (licença BSD-3-Clause, projeto descontinuado em 2023), com identidade própria (nome, hardware ID e GUIDs diferentes), para funcionar mesmo se o ViGEmBus original estiver instalado.

## Aviso antes de instalar

- O driver roda no **kernel** do Windows. Um bug pode travar o PC com tela azul. Se der, reinicie e me mande o que aconteceu.
- Ele é assinado só com um **certificado de teste**, então o Windows precisa estar em **modo de teste**, e o modo de teste exige o **Secure Boot desligado** na BIOS/UEFI. Alguns jogos com anti-cheat (Valorant, alguns da EA, etc.) se recusam a abrir com o modo de teste ligado.

## Instalar

1. Baixe o pacote `proconwin-windows-x64` na aba **Actions** do GitHub (abra a última execução verde e role até "Artifacts") e extraia numa pasta.
2. Abra o **PowerShell como Administrador** nessa pasta.
3. Ligue o modo de teste e reinicie:
   ```powershell
   bcdedit /set testsigning on
   ```
   Se der erro de "protegido pela política de Inicialização Segura", desligue o Secure Boot na BIOS e tente de novo.
4. Depois de reiniciar, confie no certificado de teste do driver:
   ```powershell
   Import-Certificate -FilePath driver\ProconWinBus.cer -CertStoreLocation Cert:\LocalMachine\Root
   Import-Certificate -FilePath driver\ProconWinBus.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
   ```
5. Instale o driver:
   ```powershell
   .\procon_install.exe install driver\ProconWinBus.inf
   ```
6. Feche a Steam, conecte o Pro Controller e rode:
   ```powershell
   .\procon_virtual.exe
   ```
   Opções: `--gyro mouse` (giroscópio move o mouse), `--gyro stick` (giroscópio soma no analógico direito), `--sens N` (sensibilidade), `--layout nome` (A da Nintendo = A do Xbox).
7. Teste em `joy.cpl` (Win+R): deve aparecer **"Controller (XBOX 360 For Windows)"**.

## Remover

```powershell
.\procon_install.exe uninstall
bcdedit /set testsigning off
```

## Compilar o driver

Precisa do Visual Studio 2022 com o [WDK](https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk) e do [DMF](https://github.com/microsoft/DMF) clonado na raiz do repositório:

```powershell
git clone --depth 1 --branch v1.1.99 https://github.com/microsoft/DMF DMF
msbuild DMF\Dmf.sln /t:"DmfK;DmfKModules_Template" /p:Configuration=Release /p:Platform=x64
msbuild driver\ProconWinBus.sln /p:Configuration=Release /p:Platform=x64
```

O resultado fica em `driver\bin\x64\`. A CI do GitHub faz exatamente isso e publica o pacote pronto.
