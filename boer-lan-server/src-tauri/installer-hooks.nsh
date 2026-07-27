!macro NSIS_HOOK_PREINSTALL
  Push $0
  Push $1
  Push $2
  Push $3

  DetailPrint "Stopping existing ${PRODUCTNAME} processes..."
  nsExec::ExecToLog 'taskkill /F /T /IM "${MAINBINARYNAME}.exe"'
  nsExec::ExecToLog 'taskkill /F /T /IM "backend-server.exe"'

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

!macro NSIS_HOOK_POSTINSTALL
  DetailPrint "Configuring Boer LAN firewall rules (LocalSubnet only)..."
  ExecShellWait "runas" "$SYSDIR\cmd.exe" '/d /s /c ""$SYSDIR\netsh.exe" advfirewall firewall delete rule name="Boer LAN Server - Management TCP" >nul 2>&1 & "$SYSDIR\netsh.exe" advfirewall firewall add rule name="Boer LAN Server - Management TCP" dir=in action=allow protocol=TCP localport=8088 remoteip=LocalSubnet profile=any enable=yes & "$SYSDIR\netsh.exe" advfirewall firewall delete rule name="Boer LAN Server - Device TCP" >nul 2>&1 & "$SYSDIR\netsh.exe" advfirewall firewall add rule name="Boer LAN Server - Device TCP" dir=in action=allow protocol=TCP localport=38400 remoteip=LocalSubnet profile=any enable=yes"' SW_HIDE
!macroend

!macro NSIS_HOOK_PREUNINSTALL
  DetailPrint "Removing Boer LAN firewall rules..."
  ExecShellWait "runas" "$SYSDIR\cmd.exe" '/d /s /c ""$SYSDIR\netsh.exe" advfirewall firewall delete rule name="Boer LAN Server - Management TCP" >nul 2>&1 & "$SYSDIR\netsh.exe" advfirewall firewall delete rule name="Boer LAN Server - Device TCP" >nul 2>&1"' SW_HIDE
!macroend
