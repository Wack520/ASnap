function(add_ai_test test_name source_file)
    qt_add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE ai_screenshot_core Qt6::Test)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:${test_name}>
    )
endfunction()

add_ai_test(test_request_guard test_request_guard.cpp)
add_ai_test(test_config_and_session test_config_and_session.cpp)
add_ai_test(test_ai_layer test_ai_layer.cpp)
add_ai_test(test_network_transport test_network_transport.cpp)
add_ai_test(test_capture_flow test_capture_flow.cpp)
add_ai_test(test_ui_widgets test_ui_widgets.cpp)
add_ai_test(test_application_controller test_application_controller.cpp)
