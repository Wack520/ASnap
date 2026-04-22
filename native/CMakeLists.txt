cmake_minimum_required(VERSION 3.28)

project(ai_screenshot_native VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Network Test)

qt_standard_project_setup()

enable_testing()

add_library(ai_screenshot_core STATIC
    src/ai/ai_client.cpp
    src/ai/ai_client.h
    src/ai/network_transport.h
    src/ai/provider_factory.cpp
    src/ai/provider_factory.h
    src/ai/provider_interface.h
    src/ai/provider_test_runner.cpp
    src/ai/provider_test_runner.h
    src/ai/qt_network_transport.cpp
    src/ai/qt_network_transport.h
    src/ai/request_spec.h
    src/ai/sample_image_factory.cpp
    src/ai/sample_image_factory.h
    src/app/application_controller.cpp
    src/app/application_controller.h
    src/app/conversation_request_controller.cpp
    src/app/conversation_request_controller.h
    src/capture/capture_overlay.cpp
    src/capture/capture_overlay.h
    src/capture/capture_selection.h
    src/capture/desktop_capture_service.cpp
    src/capture/desktop_capture_service.h
    src/capture/desktop_snapshot.h
    src/chat/chat_message.h
    src/chat/chat_session.cpp
    src/chat/chat_session.h
    src/config/app_config.h
    src/config/config_store.cpp
    src/config/config_store.h
    src/config/provider_preset.h
    src/config/provider_profile.h
    src/config/provider_protocol.h
    src/platform/windows/dpi_awareness.cpp
    src/platform/windows/dpi_awareness.h
    src/platform/windows/global_hotkey_host.cpp
    src/platform/windows/global_hotkey_host.h
    src/platform/windows/hotkey_chord.cpp
    src/platform/windows/hotkey_chord.h
    src/platform/windows/native_screen_capture.cpp
    src/platform/windows/native_screen_capture.h
    src/platform/windows/windows_graphics_capture_backend.cpp
    src/platform/windows/windows_graphics_capture_backend.h
    src/platform/windows/startup_registry.cpp
    src/platform/windows/startup_registry.h
    src/ui/icon_factory.cpp
    src/ui/icon_factory.h
    src/ui/chat/chat_markdown_renderer.cpp
    src/ui/chat/chat_markdown_renderer.h
    src/ui/chat/code_syntax_highlighter.cpp
    src/ui/chat/code_syntax_highlighter.h
    src/ui/chat/floating_chat_panel.cpp
    src/ui/chat/floating_chat_panel.h
    src/ui/chat/floating_chat_panel_helpers.cpp
    src/ui/chat/floating_chat_panel_helpers.h
    src/ui/panel_placement.cpp
    src/ui/panel_placement.h
    src/ui/settings/settingsdialog.cpp
    src/ui/settings/settingsdialog_appearance_helpers.cpp
    src/ui/settings/settingsdialog_appearance_helpers.h
    src/ui/settings/settingsdialog_appearance_section.cpp
    src/ui/settings/settingsdialog_appearance_section.h
    src/ui/settings/settingsdialog.h
    resources/branding.qrc
)
target_include_directories(ai_screenshot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ai_screenshot_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

if(WIN32)
    target_link_libraries(ai_screenshot_core PUBLIC user32 shcore dwmapi gdi32 d3d11 dxgi windowsapp)
endif()

add_executable(ai_screenshot WIN32 src/main.cpp resources/asnap.rc)
target_link_libraries(ai_screenshot PRIVATE ai_screenshot_core)
set_target_properties(ai_screenshot PROPERTIES
    OUTPUT_NAME "ASnap"
)

add_subdirectory(tests)
