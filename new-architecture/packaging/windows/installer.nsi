Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
Name "$(ProductName) ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\BohrQt\${PRODUCT}"
InstallDirRegKey HKCU "Software\BoerQt\${PRODUCT}" "InstallDir"
RequestExecutionLevel user
BrandingText "Bohr LAN"
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName" "博尔物联网${APP_LABEL}"
VIAddVersionKey /LANG=1033 "ProductName" "Bohr LAN ${APP_LABEL_EN}"
VIAddVersionKey /LANG=2052 "FileDescription" "博尔物联网${APP_LABEL}安装程序"
VIAddVersionKey /LANG=1033 "FileDescription" "Bohr LAN ${APP_LABEL_EN} Setup"
VIAddVersionKey /LANG=2052 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Bohr LAN"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Bohr LAN"
SetCompressor /SOLID lzma
!define MUI_ABORTWARNING
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${BRANDING_DIR}/${PRODUCT}-welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${BRANDING_DIR}/${PRODUCT}-welcome.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${BRANDING_DIR}/${PRODUCT}-header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "${BRANDING_DIR}/${PRODUCT}-header.bmp"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "$(LaunchApp)"
!define MUI_LANGDLL_ALLLANGUAGES
!define MUI_LANGDLL_ALWAYSSHOW
!define MUI_LANGDLL_WINDOWTITLE "语言 / Language"
!define MUI_LANGDLL_INFO "请选择安装语言。$\r$\nPlease select the installer language."
!define MUI_LANGDLL_REGISTRY_ROOT HKCU
!define MUI_LANGDLL_REGISTRY_KEY "Software\BoerQt\${PRODUCT}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_RESERVEFILE_LANGDLL

LangString ProductName ${LANG_SIMPCHINESE} "博尔物联网${APP_LABEL}"
LangString ProductName ${LANG_ENGLISH} "Bohr LAN ${APP_LABEL_EN}"
LangString LaunchApp ${LANG_SIMPCHINESE} "启动博尔物联网${APP_LABEL}"
LangString LaunchApp ${LANG_ENGLISH} "Launch Bohr LAN ${APP_LABEL_EN}"
LangString RequireX64 ${LANG_SIMPCHINESE} "此安装包需要 64 位 Windows。"
LangString RequireX64 ${LANG_ENGLISH} "This installer requires 64-bit Windows."
LangString RequireWin10 ${LANG_SIMPCHINESE} "此安装包需要 Windows 10 或更高版本。"
LangString RequireWin10 ${LANG_ENGLISH} "This installer requires Windows 10 or later."
LangString CheckFailed ${LANG_SIMPCHINESE} "无法检查运行中的程序。请确认已保存工作并退出${APP_LABEL}后继续安装。"
LangString CheckFailed ${LANG_ENGLISH} "Unable to check running applications. Save your work and close the ${APP_LABEL_EN} before continuing."
LangString CloseApp ${LANG_SIMPCHINESE} "请先保存工作并退出${APP_LABEL}，再点击重试。服务端升级前请备份数据库。"
LangString CloseApp ${LANG_ENGLISH} "Save your work and close the ${APP_LABEL_EN}, then click Retry. Back up the database before upgrading the Server."
LangString InstallSection ${LANG_SIMPCHINESE} "安装"
LangString InstallSection ${LANG_ENGLISH} "Install"

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "$(RequireX64)"
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "$(RequireWin10)"
    Abort
  ${EndIf}
  SetShellVarContext current
  InitPluginsDir
  File "/oname=$PLUGINSDIR\close-running-app.ps1" "${CLOSE_PROCESS_SCRIPT}"
check_running:
  nsExec::ExecToStack '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\close-running-app.ps1"'
  Pop $0
  Pop $1
  ${If} $0 != 0
    MessageBox MB_ICONEXCLAMATION|MB_RETRYCANCEL "$(CloseApp)" IDRETRY check_running
    Abort
  ${EndIf}
FunctionEnd

Function un.onInit
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

Section "$(InstallSection)"
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}/*"
  ; Remove the legacy executable only after the new payload is installed.
  Delete "$INSTDIR\boer-qt-${PRODUCT}.exe"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\BoerQt\${PRODUCT}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoerQt-${PRODUCT}" "DisplayName" "$(ProductName)"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoerQt-${PRODUCT}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoerQt-${PRODUCT}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoerQt-${PRODUCT}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoerQt-${PRODUCT}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoerQt-${PRODUCT}" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\博尔物联网"
  CreateShortcut "$SMPROGRAMS\博尔物联网\${APP_LABEL}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$DESKTOP\博尔物联网${APP_LABEL}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

Section "Uninstall"
  !include "${UNINSTALL_FILES}"
  Delete "$INSTDIR\boer-qt-${PRODUCT}.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\博尔物联网\${APP_LABEL}.lnk"
  RMDir "$SMPROGRAMS\博尔物联网"
  Delete "$DESKTOP\博尔物联网${APP_LABEL}.lnk"
  DeleteRegKey HKCU "Software\BoerQt\${PRODUCT}"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoerQt-${PRODUCT}"
SectionEnd
