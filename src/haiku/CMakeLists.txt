# Hydrogen, Copyright(c) 2008-2026 The hydrogen development team. GPL-2.0-or-later.
# Haiku port: Copyright(c) 2026 idumych (https://github.com/idumych)
# The native BeOS-API GUI (Hydrogen) and test tools for the port.

include_directories(
    ${CMAKE_SOURCE_DIR}/src                     # top level headers
    ${CMAKE_BINARY_DIR}/src                     # generated config.h
    ${QT_INCLUDES}
)

add_executable(h2play tools/h2play.cpp)
set_property(TARGET h2play PROPERTY CXX_STANDARD 17)
target_link_libraries(h2play
    hydrogen-core-${VERSION}
    Qt${QT_VERSION_MAJOR}::Core
)
add_dependencies(h2play hydrogen-core-${VERSION})

add_executable(h2miditest tools/h2miditest.cpp)
set_property(TARGET h2miditest PROPERTY CXX_STANDARD 17)
target_link_libraries(h2miditest
    hydrogen-core-${VERSION}
    Qt${QT_VERSION_MAJOR}::Core
    ${MIDIKIT_LIBRARIES}
)
add_dependencies(h2miditest hydrogen-core-${VERSION})

add_executable(Hydrogen
    App.cpp
    Colors.cpp
    ComponentEditor.cpp
    Controls.cpp
    DrumkitWindow.cpp
    EffectWindow.cpp
    ExportWindows.cpp
    InstrumentParameters.cpp
    LicenseField.cpp
    MainWindow.cpp
    Mixer.cpp
    NameWindow.cpp
    PatternActions.cpp
    PatternEditor.cpp
    PropertiesWindow.cpp
    Rack.cpp
    SampleEditor.cpp
    Settings.cpp
    SettingsWindow.cpp
    SongActions.cpp
    SongEditor.cpp
    SoundLibrary.cpp
    TransportBar.cpp
    UndoStack.cpp
)
set_property(TARGET Hydrogen PROPERTY CXX_STANDARD 17)
target_link_libraries(Hydrogen
    hydrogen-core-${VERSION}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    be tracker translation localestub
)
add_dependencies(Hydrogen hydrogen-core-${VERSION})
# BDecimalSpinner and BAboutWindow are only private headers on R1/beta6,
# and they include their siblings by bare name.
find_path(HAIKU_PRIVATE_INTERFACE_DIR DecimalSpinner.h
    PATHS /boot/system/develop/headers/private/interface NO_DEFAULT_PATH)
if(HAIKU_PRIVATE_INTERFACE_DIR)
    target_include_directories(Hydrogen PRIVATE ${HAIKU_PRIVATE_INTERFACE_DIR})
endif()

# Compile the resources and attach them, then register the MIME info.
set(HYDROGEN_RSRC ${CMAKE_CURRENT_BINARY_DIR}/Hydrogen.rsrc)
add_custom_command(OUTPUT ${HYDROGEN_RSRC}
    COMMAND rc -o ${HYDROGEN_RSRC} ${CMAKE_CURRENT_SOURCE_DIR}/Hydrogen.rdef
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Hydrogen.rdef)
add_custom_target(HydrogenResources DEPENDS ${HYDROGEN_RSRC})
add_dependencies(Hydrogen HydrogenResources)
add_custom_command(TARGET Hydrogen POST_BUILD
    COMMAND xres -o $<TARGET_FILE:Hydrogen> ${HYDROGEN_RSRC}
    COMMAND mimeset -f $<TARGET_FILE:Hydrogen>)

install(TARGETS Hydrogen RUNTIME DESTINATION ${H2_BIN_PATH})
