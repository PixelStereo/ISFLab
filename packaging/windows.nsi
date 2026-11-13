Unicode true
!ifndef APP_NAME
  !define APP_NAME "ISFLab"
!endif
!ifndef APP_EXE
  !define APP_EXE "ISFLab.exe"
!endif
Name "${APP_NAME}"
!ifndef TAG_SUFFIX
  !define TAG_SUFFIX ""
!endif
OutFile "..\ISFLab-Windows-x64${TAG_SUFFIX}-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section
  SetOutPath "$INSTDIR"
  File /r "..\package\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\bin\${APP_EXE}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
  RMDir /r "$INSTDIR"
SectionEnd
