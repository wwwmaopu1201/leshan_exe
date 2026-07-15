Unicode True
SetCompressor /SOLID lzma

!include "MUI2.nsh"

!ifndef PRODUCT_NAME
  !error "PRODUCT_NAME is required"
!endif
!ifndef PRODUCT_VERSION
  !error "PRODUCT_VERSION is required"
!endif
!ifndef PRODUCT_EXE
  !error "PRODUCT_EXE is required"
!endif
!ifndef INSTALL_DIR_NAME
  !error "INSTALL_DIR_NAME is required"
!endif
!ifndef APP_ID
  !error "APP_ID is required"
!endif
!ifndef SOURCE_DIR
  !error "SOURCE_DIR is required"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required"
!endif
!ifndef ICON_FILE
  !error "ICON_FILE is required"
!endif

!define APP_REG_KEY "Software\BoerLAN\${APP_ID}"
!define UNINSTALL_REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\${INSTALL_DIR_NAME}"
InstallDirRegKey HKCU "${APP_REG_KEY}" "InstallLocation"
RequestExecutionLevel user
BrandingText "Boer LAN"
ShowInstDetails show
ShowUninstDetails show

!define MUI_ABORTWARNING
!define MUI_ICON "${ICON_FILE}"
!define MUI_UNICON "${ICON_FILE}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${PRODUCT_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${PRODUCT_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Section "Install" SEC_MAIN
  SetShellVarContext current

  DetailPrint "Stopping an existing ${PRODUCT_NAME} process..."
  nsExec::ExecToLog 'taskkill /F /T /IM "${PRODUCT_EXE}"'
!ifdef BACKEND_PROCESS
  nsExec::ExecToLog 'taskkill /F /T /IM "${BACKEND_PROCESS}"'
!endif

  RMDir /r "$INSTDIR"
  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*.*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0 SW_SHOWNORMAL "" "${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0 SW_SHOWNORMAL "" "${PRODUCT_NAME}"

  WriteRegStr HKCU "${APP_REG_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayIcon" "$INSTDIR\${PRODUCT_EXE}"
  WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "Publisher" "Boer"
  WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "UninstallString" '$"$INSTDIR\Uninstall.exe$"'
  WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current

  nsExec::ExecToLog 'taskkill /F /T /IM "${PRODUCT_EXE}"'
!ifdef BACKEND_PROCESS
  nsExec::ExecToLog 'taskkill /F /T /IM "${BACKEND_PROCESS}"'
!endif

  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
  RMDir /r "$SMPROGRAMS\${PRODUCT_NAME}"
  RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "${APP_REG_KEY}"
  DeleteRegKey HKCU "${UNINSTALL_REG_KEY}"
SectionEnd
