NewTargetSource()
AddSourceFolder(INCLUDE RECURSE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public")
AddSourceFolder(RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/private")
source_group(TREE ${PROJECT_SOURCE_DIR} FILES ${SourceFiles})

function(add_lib TARGET_NAME)
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "hook")
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)

    AddTargetInclude(${TARGET_NAME})

    target_link_libraries(${TARGET_NAME} PRIVATE inihpp_a)
    target_link_libraries(${TARGET_NAME} PRIVATE libuv::uv)
    install(TARGETS ${TARGET_NAME}
        EXPORT ${TARGET_NAME}Targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        PUBLIC_HEADER DESTINATION include
    )

    install(EXPORT ${TARGET_NAME}Targets
        FILE ${TARGET_NAME}Targets.cmake
        NAMESPACE UTILPP::
        DESTINATION lib/cmake/${TARGET_NAME}
    )
endfunction()

set(TARGET_NAME inihpp_a)
install(TARGETS ${TARGET_NAME}
    EXPORT ${TARGET_NAME}Targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)

install(EXPORT ${TARGET_NAME}Targets
    FILE ${TARGET_NAME}Targets.cmake
    NAMESPACE UTILPP::
    DESTINATION lib/cmake/${TARGET_NAME}
)

set(TARGET_NAME game_captor)
add_library(${TARGET_NAME} SHARED ${SourceFiles})
add_lib(${TARGET_NAME})
target_compile_definitions(${TARGET_NAME} PRIVATE -DGAME_CAPTURE_EXPORTS)
target_link_libraries(${TARGET_NAME} PRIVATE simple_utilpp)
target_link_libraries(${TARGET_NAME} PRIVATE ipc_util)
target_link_libraries(${TARGET_NAME} PRIVATE windows_util)
target_link_libraries(${TARGET_NAME} PRIVATE simple_logger)
target_link_libraries(${TARGET_NAME} PUBLIC graphic_subsystem)
target_link_libraries(${TARGET_NAME} PUBLIC simple_hook_helper)

set(TARGET_NAME game_captor_a)
add_library(${TARGET_NAME} STATIC ${SourceFiles})
add_lib(${TARGET_NAME})
target_compile_definitions(${TARGET_NAME} PUBLIC -DGAME_CAPTURE_NODLL)
target_link_libraries(${TARGET_NAME} PRIVATE simple_utilpp_a)
target_link_libraries(${TARGET_NAME} PRIVATE ipc_util_a)
target_link_libraries(${TARGET_NAME} PRIVATE windows_util_a)
target_link_libraries(${TARGET_NAME} PRIVATE simple_logger_a)
target_link_libraries(${TARGET_NAME} PUBLIC graphic_subsystem_a)
target_link_libraries(${TARGET_NAME} PUBLIC simple_hook_helper_a)
