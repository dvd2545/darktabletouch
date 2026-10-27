find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2)
include_directories(${DARKTABLE_BINDIR})
find_package(PkgConfig REQUIRED)
pkg_check_modules(TOUCH_JSON REQUIRED IMPORTED_TARGET json-glib-1.0)

# keep Touch's modifier browser in sync with Classic's completion catalog
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ../gui/gtkentry.c)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../gui/gtkentry.c" modifier_source)
string(REGEX MATCH "static dt_gtkentry_completion_spec _default_path_compl_list\\[\\][^;]+;" modifier_table "${modifier_source}")
if(NOT modifier_table)
  message(FATAL_ERROR "Could not read the native filename modifier catalog")
endif()
string(REPLACE "static dt_gtkentry_completion_spec _default_path_compl_list" "static const struct { const char *varname; const char *description; } _touch_modifier_specs" modifier_table "${modifier_table}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/modifier_catalog.h" "${modifier_table}\n")
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Qt supplies its own Unicode Windows entry point
string(REPLACE "-municode" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

add_library(dt_touch_engine STATIC engine_bridge.c ../mcp/dt_bridge.c)
target_link_libraries(dt_touch_engine PUBLIC lib_darktable PkgConfig::TOUCH_JSON)

qt_add_executable(darktable-touch main.cpp TouchController.cpp TouchController.h Gamepad.cpp Gamepad.h)
qt_add_resources(darktable-touch touch_icon PREFIX "/" BASE "${CMAKE_SOURCE_DIR}/data/pixmaps"
  FILES "${CMAKE_SOURCE_DIR}/data/pixmaps/darktable-touch.ico")
target_link_libraries(darktable-touch PRIVATE dt_touch_engine Qt6::Quick Qt6::QuickControls2)
set_target_properties(darktable-touch PROPERTIES
  AUTOMOC ON
  RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})

set_source_files_properties(qml/RatingFilter.qml PROPERTIES QT_RESOURCE_ALIAS RatingFilter.qml)
set_source_files_properties(qml/UiMetrics.qml PROPERTIES QT_RESOURCE_ALIAS UiMetrics.qml)
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/NamePatternEditor.qml PROPERTIES QT_RESOURCE_ALIAS NamePatternEditor.qml)
set_source_files_properties(qml/TouchSwitch.qml PROPERTIES QT_RESOURCE_ALIAS TouchSwitch.qml)
set_source_files_properties(qml/TouchButton.qml PROPERTIES QT_RESOURCE_ALIAS TouchButton.qml)
set_source_files_properties(qml/FieldControl.qml PROPERTIES QT_RESOURCE_ALIAS FieldControl.qml)
set_source_files_properties(qml/PanelHandle.qml PROPERTIES QT_RESOURCE_ALIAS PanelHandle.qml)
set_source_files_properties(qml/LibraryDialog.qml PROPERTIES QT_RESOURCE_ALIAS LibraryDialog.qml)
set_source_files_properties(qml/RuleEditor.qml PROPERTIES QT_RESOURCE_ALIAS RuleEditor.qml)
set_source_files_properties(qml/ImportReview.qml PROPERTIES QT_RESOURCE_ALIAS ImportReview.qml)
set_source_files_properties(qml/TouchComboBox.qml PROPERTIES QT_RESOURCE_ALIAS TouchComboBox.qml)
set_source_files_properties(qml/ExportDialog.qml PROPERTIES QT_RESOURCE_ALIAS ExportDialog.qml)
set_source_files_properties(qml/CropOverlay.qml PROPERTIES QT_RESOURCE_ALIAS CropOverlay.qml)
set_source_files_properties(qml/Icon.qml PROPERTIES QT_RESOURCE_ALIAS Icon.qml)
set_source_files_properties(qml/TouchDialog.qml PROPERTIES QT_RESOURCE_ALIAS TouchDialog.qml)
set_source_files_properties(qml/TouchPopup.qml PROPERTIES QT_RESOURCE_ALIAS TouchPopup.qml)
set_source_files_properties(qml/ThumbnailButton.qml PROPERTIES QT_RESOURCE_ALIAS ThumbnailButton.qml)
set_source_files_properties(qml/ThumbnailImage.qml PROPERTIES QT_RESOURCE_ALIAS ThumbnailImage.qml)
set_source_files_properties(qml/ChoiceDelegate.qml PROPERTIES QT_RESOURCE_ALIAS ChoiceDelegate.qml)
set_source_files_properties(qml/HistoryPasteDialog.qml PROPERTIES QT_RESOURCE_ALIAS HistoryPasteDialog.qml)
qt_add_qml_module(darktable-touch
  URI DarktableTouch
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES qml/RatingFilter.qml qml/NamePatternEditor.qml qml/TouchSwitch.qml qml/UiMetrics.qml qml/HistoryPasteDialog.qml qml/ThumbnailButton.qml qml/ThumbnailImage.qml qml/ChoiceDelegate.qml qml/TouchComboBox.qml qml/ExportDialog.qml qml/CropOverlay.qml qml/Icon.qml qml/TouchDialog.qml qml/TouchPopup.qml qml/Main.qml qml/TouchButton.qml qml/FieldControl.qml qml/PanelHandle.qml qml/LibraryDialog.qml qml/RuleEditor.qml qml/ImportReview.qml)

qt_add_executable(darktabletouch launcher.cpp)
if(WIN32)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/touch-icon.rc" "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/data/pixmaps/darktable-touch.ico\"\n")
  target_sources(darktable-touch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/touch-icon.rc")
  target_sources(darktabletouch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/touch-icon.rc")
endif()
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
