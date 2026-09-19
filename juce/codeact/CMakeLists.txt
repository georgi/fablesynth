include(FetchContent)

set(FABLE_CODEACT_QUICKJS_SOURCE_DIR "" CACHE PATH
    "Existing QuickJS-NG checkout; otherwise fetch pinned v0.10.1")

if(NOT TARGET qjs)
    set(QJS_BUILD_LIBC OFF CACHE BOOL "Do not link OS/standard-library modules" FORCE)
    set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    if(FABLE_CODEACT_QUICKJS_SOURCE_DIR)
        add_subdirectory("${FABLE_CODEACT_QUICKJS_SOURCE_DIR}"
                         "${CMAKE_CURRENT_BINARY_DIR}/quickjs" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(fable_codeact_quickjs
            GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
            GIT_TAG v0.10.1
            GIT_SHALLOW TRUE)
        FetchContent_GetProperties(fable_codeact_quickjs)
        if(NOT fable_codeact_quickjs_POPULATED)
            FetchContent_Populate(fable_codeact_quickjs)
            add_subdirectory("${fable_codeact_quickjs_SOURCE_DIR}"
                             "${fable_codeact_quickjs_BINARY_DIR}" EXCLUDE_FROM_ALL)
        endif()
    endif()
endif()

set_target_properties(qjs PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Interface sources make JUCE's module translation units compile exactly once
# in each final plugin/test consumer.
add_library(fable_codeact INTERFACE)
add_library(fable::codeact ALIAS fable_codeact)
target_sources(fable_codeact INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/src/Agent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/OpenAIClient.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/QuickJsRuntime.cpp")
target_include_directories(fable_codeact INTERFACE "${CMAKE_CURRENT_LIST_DIR}/src")
target_link_libraries(fable_codeact INTERFACE juce::juce_core qjs)
target_compile_features(fable_codeact INTERFACE cxx_std_17)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(fable_codeact INTERFACE JUCE_USE_CURL=1)
endif()

add_executable(fable_codeact_parser_tests tests/parser_tests.cpp)
target_include_directories(fable_codeact_parser_tests PRIVATE src)
add_test(NAME fable_codeact_parser_tests COMMAND fable_codeact_parser_tests)

add_executable(fable_codeact_tests tests/core_tests.cpp)
target_link_libraries(fable_codeact_tests PRIVATE fable::codeact)
target_compile_definitions(fable_codeact_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
add_test(NAME fable_codeact_tests COMMAND fable_codeact_tests)
