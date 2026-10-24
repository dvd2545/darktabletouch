find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2)
include_directories(${DARKTABLE_BINDIR})
find_package(PkgConfig REQUIRED)
pkg_check_modules(TOUCH_JSON REQUIRED IMPORTED_TARGET json-glib-1.0)

# Qt supplies its own Unicode Windows entry point
string(REPLACE "-municode" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

add_library(dt_touch_engine STATIC engine_bridge.c ../mcp/dt_bridge.c)
target_link_libraries(dt_touch_engine PUBLIC lib_darktable PkgConfig::TOUCH_JSON)

qt_add_executable(darktable-touch main.cpp TouchController.cpp TouchController.h Gamepad.cpp Gamepad.h)
target_link_libraries(darktable-touch PRIVATE dt_touch_engine Qt6::Quick Qt6::QuickControls2)
set_target_properties(darktable-touch PROPERTIES
  AUTOMOC ON
  RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/TouchButton.qml PROPERTIES QT_RESOURCE_ALIAS TouchButton.qml)
set_source_files_properties(qml/FieldControl.qml PROPERTIES QT_RESOURCE_ALIAS FieldControl.qml)
set_source_files_properties(qml/PanelHandle.qml PROPERTIES QT_RESOURCE_ALIAS PanelHandle.qml)
set_source_files_properties(qml/LibraryDialog.qml PROPERTIES QT_RESOURCE_ALIAS LibraryDialog.qml)
set_source_files_properties(qml/ImportReview.qml PROPERTIES QT_RESOURCE_ALIAS ImportReview.qml)
qt_add_qml_module(darktable-touch
  URI DarktableTouch
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES qml/Main.qml qml/TouchButton.qml qml/FieldControl.qml qml/PanelHandle.qml qml/LibraryDialog.qml qml/ImportReview.qml)

qt_add_executable(darktabletouch launcher.cpp)
target_link_libraries(darktabletouch PRIVATE Qt6::Core PkgConfig::TOUCH_JSON)
set_target_properties(darktabletouch PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})

add_executable(test-touch-engine tests/engine.c)
target_link_libraries(test-touch-engine PRIVATE dt_touch_engine)
if(WIN32)
  target_link_options(test-touch-engine PRIVATE -municode)
endif()
set_target_properties(test-touch-engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})

qt_add_executable(test-touch-launcher-child tests/launcher_child.cpp)
target_link_libraries(test-touch-launcher-child PRIVATE Qt6::Core)
set_target_properties(test-touch-launcher-child PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})

install(TARGETS darktabletouch darktable-touch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DTApplication)

if(WIN32 AND NOT BUILD_MSYS2_INSTALL)
  qt_generate_deploy_qml_app_script(TARGET darktable-touch OUTPUT_SCRIPT touch_deploy
    NO_TRANSLATIONS NO_COMPILER_RUNTIME)
  install(SCRIPT ${touch_deploy} COMPONENT DTApplication)
  # headless package validation must use the packaged platform plugin too
  install(FILES $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
    DESTINATION share/qt6/plugins/platforms COMPONENT DTApplication)
  get_target_property(qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
  get_filename_component(qt_bin "${qt_core_dll}" DIRECTORY)
  install(DIRECTORY "${qt_bin}/../share/licenses/qt6-base" "${qt_bin}/../share/licenses/qt6-declarative"
    DESTINATION share/licenses/DarktableTouch COMPONENT DTApplication)
endif()
