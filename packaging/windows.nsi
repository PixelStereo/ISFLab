Unicode true
Name "ISFLab"
OutFile "..\ISFLab-Windows-x64-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\ISFLab"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section
  SetOutPath "$INSTDIR"
  File /r "..\package\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\ISFLab.lnk" "$INSTDIR\bin\ISFLab.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ISFLab" "DisplayName" "ISFLab"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ISFLab" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\ISFLab.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ISFLab"
  RMDir /r "$INSTDIR"
SectionEnd
