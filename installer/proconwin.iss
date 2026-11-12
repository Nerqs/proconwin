; Instalador do ProconWin (Inno Setup 6).
;
; Compilar (o CI faz isso):
;   iscc /DAppVersion=1.0.0 /DSourceDir=..\pacote installer\proconwin.iss
;
; O que ele faz:
;   - copia o programa para C:\Program Files\ProconWin;
;   - instala o driver do controle virtual com o instalar.ps1 (certificado
;     local deste PC, chave privada apagada no fim);
;   - cria o atalho no menu Iniciar (e na área de trabalho, se escolher) e
;     liga o "Iniciar com o Windows";
;   - numa atualização (o próprio ProconWin chama com /SILENT), fecha o
;     programa, troca os arquivos, atualiza o driver e abre o ProconWin de novo.
; O desinstalador remove o driver e o certificado. As configurações e os
; perfis (%APPDATA%\ProconWin) ficam, para uma instalação futura.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\pacote"
#endif

[Setup]
AppId={{61EC9B31-A886-4FF2-BC55-1C1A8177CDD7}
AppName=ProconWin
AppVersion={#AppVersion}
AppVerName=ProconWin {#AppVersion}
AppPublisher=ProconWin
AppPublisherURL=https://github.com/Nerqs/proconwin
AppSupportURL=https://github.com/Nerqs/proconwin/issues
AppUpdatesURL=https://github.com/Nerqs/proconwin/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\ProconWin
DefaultGroupName=ProconWin
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile=..\tools\procon_tray\res\proconwin.ico
UninstallDisplayIcon={app}\procon_tray.exe
UninstallDisplayName=ProconWin
OutputDir=..\saida
OutputBaseFilename=ProconWin-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
CloseApplications=no
LicenseFile=..\LICENSE

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[CustomMessages]
ptbr.TaskAutostart=Iniciar o ProconWin com o Windows (só na bandeja, ao lado do relógio)
ptbr.Driver=Instalando o driver do controle virtual...
ptbr.DriverFailed=O driver do controle virtual não foi instalado. O programa foi copiado, mas o controle de Xbox virtual não vai aparecer.%n%nO que aconteceu está em:%n%1%n%nMande esse arquivo para conferir.
ptbr.OpenApp=Abrir o ProconWin
ptbr.RemovingDriver=Removendo o driver do controle virtual...

[Tasks]
Name: "autostart"; Description: "{cm:TaskAutostart}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\ProconWin"; Filename: "{app}\procon_tray.exe"
Name: "{group}\Desinstalar o ProconWin"; Filename: "{uninstallexe}"
Name: "{autodesktop}\ProconWin"; Filename: "{app}\procon_tray.exe"; Tasks: desktopicon

[Registry]
; Mesmo valor que o "Iniciar com o Windows" da janela usa.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ProconWin"; ValueData: """{app}\procon_tray.exe"" --bandeja"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
; Sem skipifsilent: numa atualização (/SILENT) o ProconWin abre de novo sozinho.
; runasoriginaluser: abre como o usuário, não como administrador.
Filename: "{app}\procon_tray.exe"; Description: "{cm:OpenApp}"; Flags: nowait postinstall runasoriginaluser

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM procon_tray.exe"; Flags: runhidden; RunOnceId: "FecharTray"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM procon_virtual.exe"; Flags: runhidden; RunOnceId: "FecharVirtual"
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\desinstalar.ps1"""; Flags: runhidden waituntilterminated; StatusMsg: "{cm:RemovingDriver}"; RunOnceId: "RemoverDriver"

[UninstallDelete]
Type: files; Name: "{app}\instalar-driver.log"

[Code]
var
  NeedReboot: Boolean;

procedure CloseProconWin();
var
  Code: Integer;
begin
  // Os dois brigariam pelo controle e seguram os arquivos abertos.
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM procon_tray.exe', '', SW_HIDE,
       ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM procon_virtual.exe', '', SW_HIDE,
       ewWaitUntilTerminated, Code);
  Sleep(500);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseProconWin();
  Result := '';
end;

procedure InstallDriver();
var
  Code: Integer;
  Log, Params: String;
begin
  WizardForm.StatusLabel.Caption := CustomMessage('Driver');
  Log := ExpandConstant('{app}\instalar-driver.log');
  Params := '/C ""' + ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe') +
            '" -NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\instalar.ps1') +
            '" > "' + Log + '" 2>&1"';
  if not Exec(ExpandConstant('{cmd}'), Params, ExpandConstant('{app}'), SW_HIDE,
              ewWaitUntilTerminated, Code) then
    Code := -1;
  if Code = 3010 then
    NeedReboot := True
  else if Code <> 0 then
    SuppressibleMsgBox(FmtMessage(CustomMessage('DriverFailed'), [Log]), mbError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    InstallDriver();
end;

function NeedRestart(): Boolean;
begin
  Result := NeedReboot;
end;
