# Shared source lists for the QTrans executable and the test executables.
# These lists organize one application; they are not separate libraries.

set(QTRANS_SRC_DIR ${CMAKE_CURRENT_LIST_DIR})

set(QTRANS_RUNTIME_SOURCES
    ${QTRANS_SRC_DIR}/runtime/internal/diagnostics.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/backend.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/local_runtime.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/host/model_host.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/host/model_runtime.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/host/prompt_profiles.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/host/invocation_scheduler.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/text/utf8.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/text/utf8_stream_buffer.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/text/sentence_splitter.cpp
    ${QTRANS_SRC_DIR}/runtime/internal/text/chunker.cpp
)

set(QTRANS_APPLICATION_SOURCES
    ${QTRANS_SRC_DIR}/application/inference_service.cpp
    ${QTRANS_SRC_DIR}/application/inference_service.h
    ${QTRANS_SRC_DIR}/application/api/http_request_parser.cpp
    ${QTRANS_SRC_DIR}/application/api/http_request_parser.h
    ${QTRANS_SRC_DIR}/application/api/openai_protocol.cpp
    ${QTRANS_SRC_DIR}/application/api/openai_protocol.h
    ${QTRANS_SRC_DIR}/application/local_api_service.cpp
    ${QTRANS_SRC_DIR}/application/local_api_service.h
    ${QTRANS_SRC_DIR}/application/download_service.cpp
    ${QTRANS_SRC_DIR}/application/download_service.h
    ${QTRANS_SRC_DIR}/application/batch_controller.cpp
    ${QTRANS_SRC_DIR}/application/batch_controller.h
)

set(QTRANS_DOMAIN_SOURCES
    ${QTRANS_SRC_DIR}/domain/storage/app_paths.cpp
    ${QTRANS_SRC_DIR}/domain/settings/settings.cpp
    ${QTRANS_SRC_DIR}/domain/inference/inference_resolver.cpp
    ${QTRANS_SRC_DIR}/domain/inference/inference_types.cpp
    ${QTRANS_SRC_DIR}/domain/model-catalog/model_catalog.cpp
    ${QTRANS_SRC_DIR}/domain/model-catalog/language_list.cpp
    ${QTRANS_SRC_DIR}/domain/inference/platform_profile.cpp
    ${QTRANS_SRC_DIR}/domain/inference/runtime_capabilities.cpp
    ${QTRANS_SRC_DIR}/domain/download/download.cpp
    ${QTRANS_SRC_DIR}/domain/download/model_downloader.cpp
    ${QTRANS_SRC_DIR}/domain/batch/batch_store.cpp
    ${QTRANS_SRC_DIR}/domain/batch/batch_file_handler.cpp
    ${QTRANS_SRC_DIR}/domain/batch/batch_output_writer.cpp
)

set(QTRANS_LOGGING_SOURCES
    ${QTRANS_SRC_DIR}/logging/init.cpp
    ${QTRANS_SRC_DIR}/logging/logger.cpp
    ${QTRANS_SRC_DIR}/logging/registry.cpp
    ${QTRANS_SRC_DIR}/logging/component.cpp
    ${QTRANS_SRC_DIR}/logging/ai_trace.cpp
    ${QTRANS_SRC_DIR}/logging/console_progress.cpp
)

set(QTRANS_SHARED_SOURCES
    ${QTRANS_SRC_DIR}/shared/string_bridge.cpp
)

# Sources previously compiled into the desktop support library: application
# services, domain, logging, and shared helpers. UI and platform stay out.
set(QTRANS_APP_SUPPORT_SOURCES
    ${QTRANS_APPLICATION_SOURCES}
    ${QTRANS_DOMAIN_SOURCES}
    ${QTRANS_LOGGING_SOURCES}
    ${QTRANS_SHARED_SOURCES}
)

set(QTRANS_UI_SOURCES
    ${QTRANS_SRC_DIR}/ui/shared/theme/theme.h
    ${QTRANS_SRC_DIR}/ui/shell/mainwindow.cpp
    ${QTRANS_SRC_DIR}/ui/shell/mainwindow.h
    ${QTRANS_SRC_DIR}/ui/shell/page_id.h
    ${QTRANS_SRC_DIR}/ui/shell/shell_status_bar.cpp
    ${QTRANS_SRC_DIR}/ui/shell/shell_status_bar.h
    ${QTRANS_SRC_DIR}/ui/shell/model_unavailable_banner.cpp
    ${QTRANS_SRC_DIR}/ui/shell/model_unavailable_banner.h
    ${QTRANS_SRC_DIR}/ui/shell/preferences_page.cpp
    ${QTRANS_SRC_DIR}/ui/shell/preferences_page.h
    ${QTRANS_SRC_DIR}/ui/shared/modal_overlay.cpp
    ${QTRANS_SRC_DIR}/ui/shared/modal_overlay.h
    ${QTRANS_SRC_DIR}/ui/shared/panels/alert_panel.cpp
    ${QTRANS_SRC_DIR}/ui/shared/panels/alert_panel.h
    ${QTRANS_SRC_DIR}/ui/shared/theme/app_theme.cpp
    ${QTRANS_SRC_DIR}/ui/shared/theme/app_theme.h
    ${QTRANS_SRC_DIR}/ui/shared/panels/download_progress_panel.cpp
    ${QTRANS_SRC_DIR}/ui/shared/panels/download_progress_panel.h
    ${QTRANS_SRC_DIR}/ui/shared/media/image_utils.cpp
    ${QTRANS_SRC_DIR}/ui/shared/media/image_utils.h
    ${QTRANS_SRC_DIR}/ui/shared/panels/model_missing_panel.cpp
    ${QTRANS_SRC_DIR}/ui/shared/panels/model_missing_panel.h
    ${QTRANS_SRC_DIR}/ui/pages/models/model_page.cpp
    ${QTRANS_SRC_DIR}/ui/pages/models/model_page.h
    ${QTRANS_SRC_DIR}/ui/pages/models/model_row.cpp
    ${QTRANS_SRC_DIR}/ui/pages/models/model_row.h
    ${QTRANS_SRC_DIR}/ui/sidebar/sidebar_widget.cpp
    ${QTRANS_SRC_DIR}/ui/sidebar/sidebar_widget.h
    ${QTRANS_SRC_DIR}/ui/pages/translate/translate_page.cpp
    ${QTRANS_SRC_DIR}/ui/pages/translate/translate_page.h
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_page.cpp
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_page.h
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_queue_model.cpp
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_queue_model.h
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_table_view.cpp
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_table_view.h
    ${QTRANS_SRC_DIR}/ui/shared/widget_utils.cpp
    ${QTRANS_SRC_DIR}/ui/shared/widget_utils.h
    ${QTRANS_SRC_DIR}/ui/popup/popup_window.cpp
    ${QTRANS_SRC_DIR}/ui/popup/popup_window.h
    ${QTRANS_SRC_DIR}/ui/popup/session_controller.cpp
    ${QTRANS_SRC_DIR}/ui/popup/session_controller.h
    ${QTRANS_SRC_DIR}/ui/popup/system_tray.cpp
    ${QTRANS_SRC_DIR}/ui/popup/system_tray.h
    ${QTRANS_SRC_DIR}/resources/qtrans.qrc
)

set(QTRANS_PLATFORM_SOURCES
    ${QTRANS_SRC_DIR}/platform/single_instance/single_instance.cpp
    ${QTRANS_SRC_DIR}/platform/single_instance/single_instance.h
    ${QTRANS_SRC_DIR}/platform/clipboard/clipboard_capture.cpp
    ${QTRANS_SRC_DIR}/platform/clipboard/clipboard_capture.h
    ${QTRANS_SRC_DIR}/platform/hotkeys/hotkey_manager.h
)

set(QTRANS_PLATFORM_SOURCES_WIN
    ${QTRANS_SRC_DIR}/platform/win/clipboard_capture.cpp
    ${QTRANS_SRC_DIR}/platform/win/hotkey_manager.cpp
    ${QTRANS_SRC_DIR}/platform/single_instance/win/single_instance_activate.cpp
)

set(QTRANS_PLATFORM_SOURCES_MAC
    ${QTRANS_SRC_DIR}/platform/mac/clipboard_capture.cpp
    ${QTRANS_SRC_DIR}/platform/mac/hotkey_manager.cpp
    ${QTRANS_SRC_DIR}/platform/mac/platform_utils.cpp
    ${QTRANS_SRC_DIR}/platform/mac/platform_utils.h
    ${QTRANS_SRC_DIR}/platform/mac/popup_platform.h
    ${QTRANS_SRC_DIR}/platform/mac/popup_platform.mm
    ${QTRANS_SRC_DIR}/platform/single_instance/mac/single_instance.mm
)

set(QTRANS_APP_INCLUDE_DIRS
    ${QTRANS_SRC_DIR}
)

set(QTRANS_RUNTIME_INCLUDE_DIRS
    ${QTRANS_SRC_DIR}/runtime/include
    ${QTRANS_SRC_DIR}/runtime/internal
    ${QTRANS_SRC_DIR}/runtime/internal/host
)

function(qtrans_apply_project_definitions target)
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN>
    )
    if(QTRANS_MULTI_BACKEND_VALUE)
        target_compile_definitions(${target} PRIVATE QTRANS_MULTI_BACKEND)
    endif()
    if(QTRANS_GPU_METAL_VALUE)
        target_compile_definitions(${target} PRIVATE QTRANS_GPU_METAL)
    endif()
endfunction()
