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

set(QTRANS_TRANSLATE_SOURCES
    ${QTRANS_SRC_DIR}/translate/inference_service.cpp
    ${QTRANS_SRC_DIR}/translate/inference_service.h
    ${QTRANS_SRC_DIR}/translate/inference_types.cpp
    ${QTRANS_SRC_DIR}/translate/inference_types.h
    ${QTRANS_SRC_DIR}/translate/http_request_parser.cpp
    ${QTRANS_SRC_DIR}/translate/http_request_parser.h
    ${QTRANS_SRC_DIR}/translate/openai_protocol.cpp
    ${QTRANS_SRC_DIR}/translate/openai_protocol.h
    ${QTRANS_SRC_DIR}/translate/api_chat.h
    ${QTRANS_SRC_DIR}/translate/local_api_service.cpp
    ${QTRANS_SRC_DIR}/translate/local_api_service.h
)

set(QTRANS_DOWNLOAD_SOURCES
    ${QTRANS_SRC_DIR}/download/download_service.cpp
    ${QTRANS_SRC_DIR}/download/download_service.h
    ${QTRANS_SRC_DIR}/download/download.cpp
    ${QTRANS_SRC_DIR}/download/download.h
    ${QTRANS_SRC_DIR}/download/model_downloader.cpp
    ${QTRANS_SRC_DIR}/download/model_downloader.h
    ${QTRANS_SRC_DIR}/download/model_catalog.cpp
    ${QTRANS_SRC_DIR}/download/model_catalog.h
    ${QTRANS_SRC_DIR}/download/language_list.cpp
    ${QTRANS_SRC_DIR}/download/language_list.h
    ${QTRANS_SRC_DIR}/download/inference_resolver.cpp
    ${QTRANS_SRC_DIR}/download/inference_resolver.h
    ${QTRANS_SRC_DIR}/download/runtime_capabilities.cpp
    ${QTRANS_SRC_DIR}/download/runtime_capabilities.h
    ${QTRANS_SRC_DIR}/download/platform_profile.cpp
    ${QTRANS_SRC_DIR}/download/platform_profile.h
)

set(QTRANS_BATCH_SOURCES
    ${QTRANS_SRC_DIR}/batch/batch_controller.cpp
    ${QTRANS_SRC_DIR}/batch/batch_controller.h
    ${QTRANS_SRC_DIR}/batch/batch_entry_view.h
    ${QTRANS_SRC_DIR}/batch/batch_store.cpp
    ${QTRANS_SRC_DIR}/batch/batch_store.h
    ${QTRANS_SRC_DIR}/batch/batch_file_handler.cpp
    ${QTRANS_SRC_DIR}/batch/batch_file_handler.h
    ${QTRANS_SRC_DIR}/batch/batch_output_writer.cpp
    ${QTRANS_SRC_DIR}/batch/batch_output_writer.h
)

set(QTRANS_PROCESS_SOURCES
    ${QTRANS_SRC_DIR}/worker_host.cpp
    ${QTRANS_SRC_DIR}/worker_host.h
    ${QTRANS_SRC_DIR}/paths/app_paths.cpp
    ${QTRANS_SRC_DIR}/paths/app_paths.h
    ${QTRANS_SRC_DIR}/settings/settings.cpp
    ${QTRANS_SRC_DIR}/settings/settings.h
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

# Non-UI sources shared by the executable and the tests. Popup and instance
# stay on the executable; they are not a second library.
set(QTRANS_APP_SUPPORT_SOURCES
    ${QTRANS_TRANSLATE_SOURCES}
    ${QTRANS_DOWNLOAD_SOURCES}
    ${QTRANS_BATCH_SOURCES}
    ${QTRANS_PROCESS_SOURCES}
    ${QTRANS_LOGGING_SOURCES}
    ${QTRANS_SHARED_SOURCES}
)

set(QTRANS_UI_SOURCES
    ${QTRANS_SRC_DIR}/shared/theme/app_theme.cpp
    ${QTRANS_SRC_DIR}/shared/theme/app_theme.h
    ${QTRANS_SRC_DIR}/shared/theme/theme.h
    ${QTRANS_SRC_DIR}/ui/shell/mainwindow.cpp
    ${QTRANS_SRC_DIR}/ui/shell/mainwindow.h
    ${QTRANS_SRC_DIR}/ui/shell/model_flow.cpp
    ${QTRANS_SRC_DIR}/ui/shell/model_flow.h
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
    ${QTRANS_SRC_DIR}/ui/popup/system_tray.cpp
    ${QTRANS_SRC_DIR}/ui/popup/system_tray.h
    ${QTRANS_SRC_DIR}/resources/qtrans.qrc
)

set(QTRANS_POPUP_SOURCES
    ${QTRANS_SRC_DIR}/popup/popup_window.cpp
    ${QTRANS_SRC_DIR}/popup/popup_window.h
    ${QTRANS_SRC_DIR}/popup/session_controller.cpp
    ${QTRANS_SRC_DIR}/popup/session_controller.h
    ${QTRANS_SRC_DIR}/popup/popup_escape_queue.cpp
    ${QTRANS_SRC_DIR}/popup/popup_escape_queue.h
    ${QTRANS_SRC_DIR}/popup/popup_surface.h
    ${QTRANS_SRC_DIR}/popup/clipboard_capture.cpp
    ${QTRANS_SRC_DIR}/popup/clipboard_capture.h
    ${QTRANS_SRC_DIR}/popup/hotkey_manager.h
)

set(QTRANS_POPUP_SOURCES_WIN
    ${QTRANS_SRC_DIR}/popup/win/clipboard_capture.cpp
    ${QTRANS_SRC_DIR}/popup/win/hotkey_manager.cpp
    ${QTRANS_SRC_DIR}/popup/win/popup_surface.cpp
)

set(QTRANS_POPUP_SOURCES_MAC
    ${QTRANS_SRC_DIR}/popup/mac/clipboard_capture.cpp
    ${QTRANS_SRC_DIR}/popup/mac/hotkey_manager.cpp
    ${QTRANS_SRC_DIR}/popup/mac/platform_utils.cpp
    ${QTRANS_SRC_DIR}/popup/mac/platform_utils.h
    ${QTRANS_SRC_DIR}/popup/mac/popup_platform.h
    ${QTRANS_SRC_DIR}/popup/mac/popup_platform.mm
    ${QTRANS_SRC_DIR}/popup/mac/popup_surface.mm
)

set(QTRANS_INSTANCE_SOURCES
    ${QTRANS_SRC_DIR}/instance/single_instance.cpp
    ${QTRANS_SRC_DIR}/instance/single_instance.h
)

set(QTRANS_INSTANCE_SOURCES_WIN
    ${QTRANS_SRC_DIR}/instance/win/single_instance_activate.cpp
)

set(QTRANS_INSTANCE_SOURCES_MAC
    ${QTRANS_SRC_DIR}/instance/mac/single_instance.mm
)

set(QTRANS_APP_INCLUDE_DIRS
    ${QTRANS_SRC_DIR}
)

set(QTRANS_RUNTIME_INCLUDE_DIRS
    ${QTRANS_SRC_DIR}/runtime/include
)

set(QTRANS_RUNTIME_INTERNAL_INCLUDE_DIRS
    ${QTRANS_SRC_DIR}/runtime/internal
    ${QTRANS_SRC_DIR}/runtime/internal/host
)

# ggml-cpu may be built with LLVM OpenMP. The llama-cpp package does not
# re-export that runtime, so every target that links the static library must.
function(qtrans_link_llama_openmp target)
    if(NOT WIN32)
        return()
    endif()
    find_package(OpenMP REQUIRED)
    target_link_libraries(${target} PRIVATE OpenMP::OpenMP_C)
endfunction()

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
