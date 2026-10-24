# DarktableTouch installs alongside upstream darktable, including its registry entries
set(CPACK_COMPONENTS_ALL DTApplication)
set(CPACK_PACKAGE_NAME "darktabletouch")
set(CPACK_PACKAGE_VENDOR "DarktableTouch contributors")
set(CPACK_PACKAGE_CONTACT "https://github.com/dvd2545/darktabletouch")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Touch and Classic RAW photo editing")
set(CPACK_SYSTEM_NAME "${ARCH_STRING}")
# NSIS cannot read deep build paths, including Qt's long SPDX license filenames
set(CPACK_PACKAGE_FILE_NAME "darktabletouch-${ARCH_STRING}")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "darktabletouch-${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DarktableTouch")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "DarktableTouch")
set(CPACK_NSIS_DISPLAY_NAME "DarktableTouch")
set(CPACK_NSIS_PACKAGE_NAME "DarktableTouch")
set(CPACK_NSIS_HELP_LINK "https://github.com/dvd2545/darktabletouch")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/dvd2545/darktabletouch")
set(CPACK_NSIS_CONTACT "https://github.com/dvd2545/darktabletouch/issues")
if(BUILD_TOUCH_UI)
  set(touch_entry "darktabletouch")
  set(CPACK_PACKAGE_EXECUTABLES "darktabletouch" "DarktableTouch")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA "
    CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\DarktableTouch Classic.lnk' '$INSTDIR\\\\bin\\\\darktabletouch.exe' '--ui=classic'
  ")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA "
    Delete '$SMPROGRAMS\\\\$MUI_TEMP\\\\DarktableTouch Classic.lnk'
  ")
else()
  set(touch_entry "darktable")
  set(CPACK_PACKAGE_EXECUTABLES "darktable" "DarktableTouch Classic")
endif()
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\${touch_entry}.exe")
set(CPACK_CREATE_DESKTOP_LINKS "${touch_entry}")
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
  WriteRegStr HKLM 'SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\darktabletouch.exe' '' '$INSTDIR\\\\bin\\\\${touch_entry}.exe'
  WriteRegStr HKLM 'SOFTWARE\\\\Classes\\\\Applications\\\\darktabletouch.exe\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\${touch_entry}.exe\\\" \\\"%1\\\"'
")
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
  DeleteRegKey HKLM 'SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\darktabletouch.exe'
  DeleteRegKey HKLM 'SOFTWARE\\\\Classes\\\\Applications\\\\darktabletouch.exe'
")
foreach(extension IN LISTS DT_SUPPORTED_EXTENSIONS)
  string(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    WriteRegStr HKLM 'SOFTWARE\\\\Classes\\\\.${extension}\\\\OpenWithList\\\\darktabletouch.exe' '' ''
  ")
  string(APPEND CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    DeleteRegKey HKLM 'SOFTWARE\\\\Classes\\\\.${extension}\\\\OpenWithList\\\\darktabletouch.exe'
  ")
endforeach()
