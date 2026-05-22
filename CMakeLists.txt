cmake_minimum_required(VERSION 3.16)

project(QTrans LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(llama CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(qtrans_core STATIC
    src/core/app_paths.cpp
    src/core/model_ops.cpp
    src/core/translation_languages.cpp
    src/download/download.cpp
    src/settings/model_catalog.cpp
    src/settings/settings.cpp
    src/models/translation_model.cpp
    src/models/hymt15.cpp
)

target_include_directories(qtrans_core PUBLIC src)
target_link_libraries(qtrans_core PUBLIC llama CURL::libcurl)

add_executable(QTrans
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/backend.cpp
    src/app/backend.h
    src/app/modal_overlay.cpp
    src/app/modal_overlay.h
    src/app/ui/app_theme.cpp
    src/app/ui/app_theme.h
    src/app/ui/download_progress_panel.cpp
    src/app/ui/download_progress_panel.h
    src/app/ui/image_utils.cpp
    src/app/ui/image_utils.h
    src/app/ui/model_missing_panel.cpp
    src/app/ui/model_missing_panel.h
    src/app/ui/model_page.cpp
    src/app/ui/model_page.h
    src/app/ui/sidebar_widget.cpp
    src/app/ui/sidebar_widget.h
    src/app/ui/translate_page.cpp
    src/app/ui/translate_page.h
    src/app/widget_utils.cpp
    src/app/widget_utils.h
    resources/qtrans.qrc
)

target_link_libraries(QTrans PRIVATE qtrans_core Qt6::Widgets)

if(WIN32)
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QWindowsIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QModernWindowsStylePlugin
    )
elseif(APPLE)
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

if(MINGW)
    # llama-cpp MinGW builds disable OpenMP in overlay/llama-cpp; -static-libgomp is unsupported by MSYS2 g++.
    target_link_options(qtrans_core PUBLIC
        -static-libgcc
        -static-libstdc++
    )
    target_link_options(QTrans PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic
        -lwinpthread
        -Wl,-Bdynamic
    )
endif()

if(MSVC)
    target_compile_options(QTrans PRIVATE /utf-8)
    target_compile_options(qtrans_core PRIVATE /utf-8)
endif()

if(WIN32)
    set_target_properties(QTrans PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
