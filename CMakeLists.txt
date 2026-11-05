cmake_minimum_required(VERSION 3.22)
project(Tabletop VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found. Run scripts/bootstrap.sh first.")
endif()
add_subdirectory(external/JUCE)

# The devices: each lives in its own repository (git submodules under devices/) and is compiled into this app
# from the source list it publishes. TABLETOP_DEVICES_DIR can point at sibling checkouts while developing.
set(TABLETOP_DEVICES_DIR "${CMAKE_SOURCE_DIR}/devices" CACHE PATH "Folder holding the fourpole, valvebox, padlab and trackdesk repositories")
foreach(device fourpole valvebox padlab trackdesk)
  if(NOT EXISTS "${TABLETOP_DEVICES_DIR}/${device}/${device}-module.cmake")
    message(FATAL_ERROR "${device} not found in ${TABLETOP_DEVICES_DIR}. Run scripts/bootstrap.sh first.")
  endif()
  include("${TABLETOP_DEVICES_DIR}/${device}/${device}-module.cmake")
endforeach()

juce_add_gui_app(Tabletop
  PRODUCT_NAME "Tabletop"
  COMPANY_NAME "Tabletop Research"
  BUNDLE_ID "org.tabletop.app"
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Tabletop passes your audio interface's inputs to the devices you cable them to.")

target_sources(Tabletop PRIVATE
  src/Main.cpp
  src/studio/Registry.cpp
  src/studio/BuiltIn.cpp
  src/studio/Session.cpp
  src/ui/Wood.cpp
  src/ui/Table.cpp
  src/ui/MainComponent.cpp
  ${FOURPOLE_MODULE_SOURCES}
  ${VALVEBOX_MODULE_SOURCES}
  ${PADLAB_MODULE_SOURCES}
  ${TRACKDESK_MODULE_SOURCES})
padlab_add_assets(Tabletop)

target_include_directories(Tabletop PRIVATE src "${TABLETOP_DEVICES_DIR}")
target_compile_definitions(Tabletop PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_MP3AUDIOFORMAT=0
  JUCE_PLUGINHOST_VST3=0
  JUCE_PLUGINHOST_AU=0
  JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1
  JUCE_APPLICATION_NAME_STRING="Tabletop"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_compile_options(Tabletop PRIVATE -Wno-float-equal)
target_link_libraries(Tabletop PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_dsp
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)
