cmake_minimum_required(VERSION 3.22)

# Must be set before project(). JUCE 7 calls CGWindowListCreateImage, which the
# macOS 15+ SDKs mark unavailable when the deployment target is 15 or newer (the
# default is the host OS version), so pin an older target explicitly.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
# JUCE builds its juceaide helper in a nested CMake run that only inherits the
# environment, so pass the same target that way too.
set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")

project(SoundFunnel VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    # static CRT so the app runs without the VC++ redistributable
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8)
endif()

include(FetchContent)

# ---------------------------------------------------------------- Opus
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
if (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES ";")
    # universal build: opus picks x86 / ARM intrinsics per configure, not per slice
    set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.5.2
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(opus)

# AOO includes <opus/opus_multistream.h>; expose the headers under an opus/ prefix
set(SF_OPUS_INC "${CMAKE_BINARY_DIR}/opus_inc")
file(GLOB _opus_headers "${opus_SOURCE_DIR}/include/*.h")
file(COPY ${_opus_headers} DESTINATION "${SF_OPUS_INC}/opus")

# ---------------------------------------------------------------- AOO (from SonoBus)
set(AOO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/aoo")
add_library(aoo STATIC
    ${AOO_DIR}/lib/src/client.cpp
    ${AOO_DIR}/lib/src/codec_opus.cpp
    ${AOO_DIR}/lib/src/codec_pcm.cpp
    ${AOO_DIR}/lib/src/common.cpp
    ${AOO_DIR}/lib/src/net_utils.cpp
    ${AOO_DIR}/lib/src/server.cpp
    ${AOO_DIR}/lib/src/sink.cpp
    ${AOO_DIR}/lib/src/source.cpp
    ${AOO_DIR}/lib/src/sync.cpp
    ${AOO_DIR}/lib/src/time.cpp
    ${AOO_DIR}/deps/md5/md5.c
    ${AOO_DIR}/deps/oscpack/osc/OscOutboundPacketStream.cpp
    ${AOO_DIR}/deps/oscpack/osc/OscPrintReceivedElements.cpp
    ${AOO_DIR}/deps/oscpack/osc/OscReceivedElements.cpp
    ${AOO_DIR}/deps/oscpack/osc/OscTypes.cpp)
target_include_directories(aoo PUBLIC ${AOO_DIR}/lib ${AOO_DIR}/deps ${SF_OPUS_INC})
target_compile_definitions(aoo PUBLIC AOO_STATIC USE_CODEC_OPUS=1 AOO_TIMEFILTER_CHECK=0)
target_link_libraries(aoo PUBLIC opus)
if (WIN32)
    target_link_libraries(aoo PUBLIC ws2_32)
    target_compile_definitions(aoo PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(aoo PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------- JUCE
add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

juce_add_gui_app(SoundFunnel
    PRODUCT_NAME "SoundFunnel"
    COMPANY_NAME "wtnbgo"
    BUNDLE_ID "io.github.wtnbgo.soundfunnel"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "SoundFunnel sends the selected audio input to the server PC on your LAN."
    PLIST_TO_MERGE "<plist><dict><key>LSUIElement</key><true/><key>NSLocalNetworkUsageDescription</key><string>SoundFunnel finds the other PCs of your group on the local network and sends audio to the server PC.</string><key>NSAudioCaptureUsageDescription</key><string>SoundFunnel sends what this Mac plays to the server PC when System audio is selected.</string></dict></plist>")

target_sources(SoundFunnel PRIVATE
    src/Main.cpp
    src/AppConfig.cpp
    src/AppCore.cpp
    src/Discovery.cpp
    src/AudioEngine.cpp
    src/Codecs.cpp
    src/SettingsWindow.cpp
    src/TrayIcon.cpp
    src/Autostart.cpp
    src/Localisation.cpp
    src/SelfTest.cpp
    src/MicPermission.cpp)

if (APPLE)
    target_sources(SoundFunnel PRIVATE src/MicPermission_mac.mm src/LoopbackCapture_mac.mm)
else()
    target_sources(SoundFunnel PRIVATE src/LoopbackCapture.cpp)
endif()
if (UNIX AND NOT APPLE)
    target_sources(SoundFunnel PRIVATE src/LinuxTray.cpp)
endif()

if (APPLE)
    target_link_libraries(SoundFunnel PRIVATE "-framework AVFoundation")
endif()

target_compile_definitions(SoundFunnel PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SoundFunnel,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SoundFunnel,JUCE_VERSION>"
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_ASIO=0
    JUCE_ALSA=1
    JUCE_JACK=1)

if (APPLE)
    # ad-hoc sign the bundle so the macOS application firewall can attach an
    # "allow incoming connections" rule to it (it ignores unsigned apps)
    add_custom_command(TARGET SoundFunnel POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:SoundFunnel>"
        VERBATIM)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(SoundFunnel PRIVATE ${CMAKE_DL_LIBS})   # libpulse-simple is dlopen'ed
endif()

juce_add_binary_data(SoundFunnelData SOURCES resources/icon.png)

target_link_libraries(SoundFunnel PRIVATE
    SoundFunnelData
    aoo
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- install / packages
# Only the "app" component is packaged (opus would otherwise add its own install rules).
if (APPLE)
    install(TARGETS SoundFunnel BUNDLE DESTINATION . COMPONENT app)
elseif (WIN32)
    install(TARGETS SoundFunnel RUNTIME DESTINATION . COMPONENT app)
    install(FILES LICENSE README.md DESTINATION . COMPONENT app)
else()
    include(GNUInstallDirs)
    install(TARGETS SoundFunnel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
    install(FILES packaging/linux/soundfunnel.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT app)
    install(FILES resources/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/1024x1024/apps
            RENAME soundfunnel.png COMPONENT app)
    install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT app)
endif()

set(CPACK_PACKAGE_NAME "soundfunnel")
set(CPACK_PACKAGE_VENDOR "SoundFunnel contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Gather audio from several PCs on a LAN onto one server PC")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wtnbgo/soundfunnel")
set(CPACK_PACKAGE_CONTACT "https://github.com/wtnbgo/soundfunnel/issues")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};SoundFunnel;app;/")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libpulse0, fonts-noto-cjk, libayatana-appindicator3-1")   # system audio, Japanese UI, tray (all dlopen / optional)
include(CPack)
