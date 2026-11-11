# Instala o driver do controle virtual (ProconWinXusb) sem modo de teste e
# sem mexer no Secure Boot.
#
# Rode na pasta do pacote, num PowerShell como Administrador:
#   powershell -ExecutionPolicy Bypass -File .\instalar.ps1
#
# O que ele faz:
#  1. Cria um certificado de assinatura que só existe neste PC.
#  2. Coloca esse certificado como confiável neste PC.
#  3. Assina o pacote do driver com ele.
#  4. Apaga a chave privada do certificado. Assim ninguém (nem um programa
#     malicioso) consegue usar esse certificado para assinar outra coisa.
#  5. Instala o driver.
#
# Isso só funciona porque o driver roda em modo usuário (UMDF). Um driver de
# kernel continuaria exigindo a assinatura da Microsoft.

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$driverDir = Join-Path $here 'driver'
$inf = Join-Path $driverDir 'ProconWinXusb.inf'
$cat = Join-Path $driverDir 'ProconWinXusb.cat'
$dll = Join-Path $driverDir 'ProconWinXusb.dll'
$installer = Join-Path $here 'procon_install.exe'
$subject = 'CN=ProconWin (certificado local deste PC)'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).
  IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
  Write-Host 'Abra o PowerShell como Administrador e rode de novo.' -ForegroundColor Red
  exit 1
}
foreach ($f in @($inf, $cat, $dll, $installer)) {
  if (-not (Test-Path $f)) {
    Write-Host "Arquivo não encontrado: $f" -ForegroundColor Red
    exit 1
  }
}

# Arquivos baixados da internet vêm marcados como "bloqueados".
Get-ChildItem -Recurse $here | Unblock-File

# Remove certificados de instalações anteriores.
foreach ($store in @('Root', 'TrustedPublisher')) {
  Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $subject | Remove-Item
}

Write-Host '1/5 Criando o certificado local...'
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
  -CertStoreLocation Cert:\LocalMachine\My -KeyExportPolicy NonExportable `
  -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(20)

try {
  Write-Host '2/5 Marcando o certificado como confiável neste PC...'
  $cer = Join-Path $env:TEMP 'proconwin-local.cer'
  Export-Certificate -Cert $cert -FilePath $cer | Out-Null
  Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
  Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
  Remove-Item $cer

  Write-Host '3/5 Assinando o driver...'
  foreach ($f in @($dll, $cat)) {
    $sig = Set-AuthenticodeSignature -FilePath $f -Certificate $cert -HashAlgorithm SHA256
    if ($sig.Status -ne 'Valid') {
      throw "A assinatura de $f falhou: $($sig.StatusMessage)"
    }
  }
}
finally {
  Write-Host '4/5 Apagando a chave privada do certificado...'
  Remove-Item -Path "Cert:\LocalMachine\My\$($cert.Thumbprint)" -DeleteKey
}

Write-Host '5/5 Instalando o driver...'
& $installer install $inf
if ($LASTEXITCODE -ne 0) {
  Write-Host 'A instalação falhou. Mande a mensagem acima.' -ForegroundColor Red
  exit 1
}

Write-Host ''
Write-Host 'Pronto. Agora conecte o Pro Controller e rode .\procon_virtual.exe' -ForegroundColor Green
