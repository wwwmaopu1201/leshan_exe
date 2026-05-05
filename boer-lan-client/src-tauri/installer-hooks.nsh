!macro NSIS_HOOK_PREINSTALL
  Push $0
  Push $1
  Push $2
  Push $3

  DetailPrint "Stopping existing ${PRODUCTNAME} processes..."
  nsExec::ExecToLog 'taskkill /F /T /IM "${MAINBINARYNAME}.exe"'

  ReadRegStr $0 SHCTX "${MANUPRODUCTKEY}" ""
  ReadRegStr $1 SHCTX "${UNINSTKEY}" "UninstallString"
  ${If} "$1" != ""
    DetailPrint "Uninstalling previous ${PRODUCTNAME}..."
    StrCpy $3 '$1 /S _?=$0'
    ExecWait '$3' $2
    ${If} $2 <> 0
      MessageBox MB_ICONSTOP "旧版本卸载失败，安装已停止。请手动卸载旧版本后重新安装。"
      Abort
    ${EndIf}
  ${EndIf}

  ${If} "$0" != ""
    DetailPrint "Removing previous install directory: $0"
    RMDir /r "$0"
  ${EndIf}
  RMDir /r "$INSTDIR"

  DetailPrint "Clearing ${PRODUCTNAME} local data..."
  SetShellVarContext current
  RMDir /r "$APPDATA\${BUNDLEID}"
  RMDir /r "$LOCALAPPDATA\${BUNDLEID}"
  RMDir /r "$APPDATA\BoerLAN"
  RMDir /r "$LOCALAPPDATA\BoerLAN"
  DeleteRegKey SHCTX "${MANUPRODUCTKEY}"
  DeleteRegKey SHCTX "${UNINSTKEY}"
  DeleteRegValue HKCU "${MANUPRODUCTKEY}" "Installer Language"
  DeleteRegKey /ifempty HKCU "${MANUPRODUCTKEY}"
  DeleteRegKey /ifempty HKCU "${MANUKEY}"

  Pop $3
  Pop $2
  Pop $1
  Pop $0
!macroend
