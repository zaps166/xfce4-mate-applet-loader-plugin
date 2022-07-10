cmake_minimum_required(VERSION 3.8)
project(mateappletloader LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.22)
pkg_check_modules(libxfce4panel REQUIRED IMPORTED_TARGET libxfce4panel-2.0>=4.10)
pkg_check_modules(libxfce4util REQUIRED IMPORTED_TARGET libxfce4util-1.0>=4.10)

add_library(${PROJECT_NAME} MODULE
    register.c
    applet.cpp
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES CXX_VISIBILITY_PRESET hidden
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::GTK3
    PkgConfig::libxfce4panel
    PkgConfig::libxfce4util
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    DATAROOTDIR_PATH=\"${CMAKE_INSTALL_FULL_DATAROOTDIR}\"
)

install(TARGETS ${PROJECT_NAME}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/xfce4/panel/plugins"
)
install(FILES "${CMAKE_SOURCE_DIR}/mateappletloader.desktop"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/xfce4/panel/plugins"
)

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()
