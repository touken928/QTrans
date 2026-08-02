add_library(qtrans_desktop STATIC
    domain/storage/app_paths.cpp
    domain/settings/settings.cpp
    domain/inference/inference_resolver.cpp
    domain/model-catalog/model_catalog.cpp
    domain/model-catalog/language_list.cpp
    domain/inference/platform_profile.cpp
    domain/inference/runtime_capabilities.cpp
    domain/download/download.cpp
    domain/download/model_downloader.cpp
    domain/logging/init.cpp
    domain/logging/logger.cpp
    domain/logging/registry.cpp
    domain/logging/component.cpp
    domain/logging/ai_trace.cpp
    domain/logging/console_progress.cpp
    domain/batch/batch_store.cpp
    domain/batch/batch_file_handler.cpp
    app/inference_service.cpp
    app/inference_service.h
    app/local_api_service.cpp
    app/local_api_service.h
    app/download_service.cpp
    app/download_service.h
    app/batch_controller.cpp
    app/batch_controller.h
    shared/string_bridge.cpp
)

target_include_directories(qtrans_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qtrans_desktop PUBLIC qtrans_core Qt6::Core Qt6::Network)

target_link_libraries(qtrans_desktop PUBLIC
    CURL::libcurl
    spdlog::spdlog
)

target_compile_definitions(qtrans_desktop PUBLIC
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN>
)

if(QTRANS_MULTI_BACKEND_VALUE)
    target_compile_definitions(qtrans_desktop PUBLIC QTRANS_MULTI_BACKEND)
endif()

if(QTRANS_GPU_METAL_VALUE)
    target_compile_definitions(qtrans_desktop PUBLIC QTRANS_GPU_METAL)
endif()

# Test-only failure injection (src/desktop/domain/batch/batch_store.cpp) is
# compiled only when building the unit tests; production builds exclude it.
if(QTRANS_BUILD_TESTS)
    target_compile_definitions(qtrans_desktop PRIVATE QTRANS_BUILD_TESTS)
endif()

# ── QTrans executable ────────────────────────────────────────────────────────

set(_QT_SOURCES_COMMON
    ui/shared/theme/theme.h
    app/main.cpp
    domain/platform/single_instance/single_instance.cpp
    domain/platform/single_instance/single_instance.h
    ui/shell/mainwindow.cpp
    ui/shell/mainwindow.h
    ui/shell/page_id.h
    ui/shell/shell_status_bar.cpp
    ui/shell/shell_status_bar.h
    ui/shell/model_unavailable_banner.cpp
    ui/shell/model_unavailable_banner.h
    ui/shell/preferences_page.cpp
    ui/shell/preferences_page.h
    ui/shared/modal_overlay.cpp
    ui/shared/modal_overlay.h
    ui/shared/panels/alert_panel.cpp
    ui/shared/panels/alert_panel.h
    ui/shared/theme/app_theme.cpp
    ui/shared/theme/app_theme.h
    ui/shared/panels/download_progress_panel.cpp
    ui/shared/panels/download_progress_panel.h
    ui/shared/media/image_utils.cpp
    ui/shared/media/image_utils.h
    ui/shared/panels/model_missing_panel.cpp
    ui/shared/panels/model_missing_panel.h
    ui/pages/models/model_page.cpp
    ui/pages/models/model_page.h
    ui/pages/models/model_row.cpp
    ui/pages/models/model_row.h
    ui/sidebar/sidebar_widget.cpp
    ui/sidebar/sidebar_widget.h
    ui/pages/translate/translate_page.cpp
    ui/pages/translate/translate_page.h
    ui/pages/batch/batch_page.cpp
    ui/pages/batch/batch_page.h
    ui/pages/batch/batch_queue_model.cpp
    ui/pages/batch/batch_queue_model.h
    ui/pages/batch/batch_table_view.cpp
    ui/pages/batch/batch_table_view.h
    ui/shared/widget_utils.cpp
    ui/shared/widget_utils.h
    ui/popup/popup_window.cpp
    ui/popup/popup_window.h
    domain/platform/clipboard/clipboard_capture.cpp
    domain/platform/clipboard/clipboard_capture.h
    domain/platform/hotkeys/hotkey_manager.h
    ui/popup/session_controller.cpp
    ui/popup/session_controller.h
    ui/popup/system_tray.cpp
    ui/popup/system_tray.h
    resources/qtrans.qrc
)

set(_QT_SOURCES_WIN
    domain/platform/win/clipboard_capture.cpp
    domain/platform/win/hotkey_manager.cpp
)

set(_QT_SOURCES_MAC
    domain/platform/mac/clipboard_capture.cpp
    domain/platform/mac/hotkey_manager.cpp
    domain/platform/mac/platform_utils.cpp
    domain/platform/mac/platform_utils.h
)

add_executable(QTrans ${_QT_SOURCES_COMMON})

target_link_libraries(QTrans PRIVATE qtrans_desktop Qt6::Widgets)

if(WIN32)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_WIN} domain/platform/single_instance/win/single_instance_activate.cpp)
elseif(APPLE)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_MAC} domain/platform/single_instance/mac/single_instance.mm)
    target_link_libraries(QTrans PRIVATE "-framework AppKit" "-framework ApplicationServices" "-framework Carbon" "-framework CoreGraphics")
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

if(MINGW)
    target_link_options(QTrans PRIVATE -static-libgcc -static-libstdc++ -static -mwindows)
endif()
