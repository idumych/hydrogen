# Native Haiku port: test tools for now, the BeOS-API GUI later.

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
