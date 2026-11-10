set(LAUNCHER
    buildsetupdialog.cpp
    datafilespage.cpp
    graphicspage.cpp
    sdlinit.cpp
    main.cpp
    updatecontroller.cpp
    maindialog.cpp
    playpage.cpp
    arenalinkclient.cpp
    chatpage.cpp
    voice/voicepanel.cpp
    textslotmsgbox.cpp
    settingspage.cpp
    advancedpage.cpp
    serverdialog.cpp

    utils/cellnameloader.cpp
    utils/profilescombobox.cpp
    utils/textinputdialog.cpp
    utils/lineedit.cpp
    utils/openalutil.cpp

    ${CMAKE_SOURCE_DIR}/files/windows/launcher.rc
)

set(LAUNCHER_HEADER
    buildsetupdialog.hpp
    datafilespage.hpp
    graphicspage.hpp
    sdlinit.hpp
    maindialog.hpp
    playpage.hpp
    arenalinkclient.hpp
    chatpage.hpp
    voice/voicepanel.hpp
    textslotmsgbox.hpp
    settingspage.hpp
    advancedpage.hpp
    serverdialog.hpp

    utils/cellnameloader.hpp
    utils/profilescombobox.hpp
    utils/textinputdialog.hpp
    utils/lineedit.hpp
    utils/openalutil.hpp
)

# Headers that must be pre-processed
set(LAUNCHER_HEADER_MOC
    buildsetupdialog.hpp
    datafilespage.hpp
    graphicspage.hpp
    maindialog.hpp
    playpage.hpp
    arenalinkclient.hpp
    chatpage.hpp
    voice/voicepanel.hpp
    textslotmsgbox.hpp
    settingspage.hpp
    advancedpage.hpp
    serverdialog.hpp

    utils/cellnameloader.hpp
    utils/textinputdialog.hpp
    utils/profilescombobox.hpp
    utils/lineedit.hpp
    utils/openalutil.hpp

)

set(LAUNCHER_UI
    ${CMAKE_SOURCE_DIR}/files/ui/datafilespage.ui
    ${CMAKE_SOURCE_DIR}/files/ui/graphicspage.ui
    ${CMAKE_SOURCE_DIR}/files/ui/mainwindow.ui
    ${CMAKE_SOURCE_DIR}/files/ui/playpage.ui
    ${CMAKE_SOURCE_DIR}/files/ui/contentselector.ui
    ${CMAKE_SOURCE_DIR}/files/ui/settingspage.ui
    ${CMAKE_SOURCE_DIR}/files/ui/advancedpage.ui
)

source_group(launcher FILES ${LAUNCHER} ${LAUNCHER_HEADER})

set(QT_USE_QTGUI 1)

# Set some platform specific settings
if(WIN32)
    set(GUI_TYPE WIN32)
    set(QT_USE_QTMAIN TRUE)
endif(WIN32)

QT5_ADD_RESOURCES(RCC_SRCS ${CMAKE_SOURCE_DIR}/files/launcher/launcher.qrc)
QT5_ADD_RESOURCES(ARENA_GLASS_RCC ${CMAKE_SOURCE_DIR}/components/misc/arenaicons.qrc)
QT5_WRAP_CPP(MOC_SRCS ${LAUNCHER_HEADER_MOC})
QT5_WRAP_UI(UI_HDRS ${LAUNCHER_UI})

include_directories(${CMAKE_CURRENT_BINARY_DIR})
if(NOT WIN32)
    include_directories(${LIBUNSHIELD_INCLUDE_DIR})
endif(NOT WIN32)

# Main executable
openmw_add_executable(arenamp-launcher
    ${GUI_TYPE}
    ${LAUNCHER}
    ${LAUNCHER_HEADER}
    ${RCC_SRCS}
    ${ARENA_GLASS_RCC}
    ${MOC_SRCS}
    ${UI_HDRS}
)

# ArenaMP: the built-in RU translator is UTF-8 source. MSVC otherwise
# interprets non-ASCII literals using the build machine code page, which
# produces mojibake (D0/D1-style text) on Russian UI builds.
if (MSVC)
    target_compile_options(arenamp-launcher PRIVATE /utf-8)
endif()

if (WIN32)
    INSTALL(TARGETS arenamp-launcher RUNTIME DESTINATION ".")
endif (WIN32)

target_link_libraries(arenamp-launcher
    ${SDL2_LIBRARY_ONLY}
    ${OPENAL_LIBRARY}
    components
)

target_link_libraries(arenamp-launcher Qt5::Widgets Qt5::Core Qt5::Gui Qt5::Network)

if (WIN32)
    target_link_libraries(arenamp-launcher dxgi advapi32 ole32 uuid)
endif (WIN32)

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(arenamp-launcher gcov)
endif()


# ArenaMP native updater. It is a normal Qt/C++ target, not a PyInstaller
# bundle. Keeping it as a separate process is intentional: on Windows it must
# remain alive after arenamp-launcher exits so it can replace the launcher and
# its DLLs transactionally.
find_package(ZLIB REQUIRED)

set(ARENA_UPDATER
    updater/arena_updater.cpp
)
if (WIN32)
    list(APPEND ARENA_UPDATER ${CMAKE_SOURCE_DIR}/files/windows/launcher.rc)
endif()

openmw_add_executable(arena-updater
    ${GUI_TYPE}
    ${ARENA_UPDATER}
    ${ARENA_GLASS_RCC}
)

target_include_directories(arena-updater PRIVATE ${ZLIB_INCLUDE_DIRS})
target_link_libraries(arena-updater
    Qt5::Widgets
    Qt5::Core
    Qt5::Gui
    ${ZLIB_LIBRARIES}
)

if (NOT WIN32)
    target_link_libraries(arena-updater Qt5::Network)
endif()

if (MSVC)
    target_compile_options(arena-updater PRIVATE /utf-8)
endif()

if (WIN32)
    # Use the Windows native HTTPS stack for updater downloads. This avoids a
    # runtime dependency on optional OpenSSL DLLs from a particular Qt build.
    target_link_libraries(arena-updater winhttp)
    install(TARGETS arena-updater RUNTIME DESTINATION ".")
elseif (NOT APPLE)
    install(TARGETS arena-updater RUNTIME DESTINATION "${BINDIR}")
endif()

# H022: native Qt/C++ relay agent. Replaces the old Python/PyInstaller helper.
if (NOT ANDROID)
    find_package(OpenSSL REQUIRED)
    set(ARENA_RELAY_AGENT
        agent-native/relay_agent_main.cpp
        agent-native/Crypto.cpp
        agent-native/Crypto.h
        agent-native/HttpJsonClient.cpp
        agent-native/HttpJsonClient.h
        agent-native/RelayLink.cpp
        agent-native/RelayLink.h
    )
    # The launcher uses explicit QT5_WRAP_CPP rather than global AUTOMOC.
    # Both relay-agent QObject subclasses have Q_OBJECT and need generated
    # meta-object definitions in the arena-relay-agent link target.
    QT5_WRAP_CPP(ARENA_RELAY_AGENT_MOC_SRCS
        agent-native/HttpJsonClient.h
        agent-native/RelayLink.h
    )
    openmw_add_executable(arena-relay-agent
        ${ARENA_RELAY_AGENT}
        ${ARENA_RELAY_AGENT_MOC_SRCS}
    )
    target_include_directories(arena-relay-agent PRIVATE agent-native)
    target_link_libraries(arena-relay-agent Qt5::Core Qt5::Network OpenSSL::Crypto)
    if (MSVC)
        target_compile_options(arena-relay-agent PRIVATE /utf-8)
    endif()
    install(TARGETS arena-relay-agent RUNTIME DESTINATION ".")
endif()

if(NOT ANDROID)
 QT5_WRAP_CPP(ARENA_PUBLISHER_MOC agent-native/ServerPublisher.h agent-native/HttpJsonClient.h agent-native/RelayLink.h)
 openmw_add_executable(arena-server-agent agent-native/server_agent_main.cpp agent-native/ServerPublisher.cpp agent-native/HttpJsonClient.cpp agent-native/RelayLink.cpp agent-native/Crypto.cpp ${ARENA_PUBLISHER_MOC})
 target_include_directories(arena-server-agent PRIVATE agent-native)
 target_link_libraries(arena-server-agent Qt5::Core Qt5::Network OpenSSL::Crypto)
 if(MSVC)
  target_compile_options(arena-server-agent PRIVATE /utf-8)
 endif()
 if(WIN32)
  install(TARGETS arena-server-agent RUNTIME DESTINATION ".")
 else()
  install(TARGETS arena-server-agent arena-relay-agent RUNTIME DESTINATION "${BINDIR}")
 endif()
endif()
