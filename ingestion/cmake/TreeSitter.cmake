include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Pin the runtime and grammars together; verify every downloaded archive.
set(ODYSSEUS_TS_VERSION "0.24.7")
set(ODYSSEUS_TS_C_VERSION "0.23.4")
set(ODYSSEUS_TS_CPP_VERSION "0.23.4")
FetchContent_Declare(odysseus_ts_runtime
    URL https://codeload.github.com/tree-sitter/tree-sitter/tar.gz/refs/tags/v${ODYSSEUS_TS_VERSION}
    URL_HASH SHA256=7cbc13c974d6abe978cafc9da12d1e79e07e365c42af75e43ec1b5cdc03ed447
    SOURCE_SUBDIR unused)
FetchContent_Declare(odysseus_ts_c_source
    URL https://codeload.github.com/tree-sitter/tree-sitter-c/tar.gz/refs/tags/v${ODYSSEUS_TS_C_VERSION}
    URL_HASH SHA256=b66c5043e26d84e5f17a059af71b157bcf202221069ed220aa1696d7d1d28a7a
    SOURCE_SUBDIR unused)
FetchContent_Declare(odysseus_ts_cpp_source
    URL https://codeload.github.com/tree-sitter/tree-sitter-cpp/tar.gz/refs/tags/v${ODYSSEUS_TS_CPP_VERSION}
    URL_HASH SHA256=7a2c55afe3028f4105f25762ea58cc16537d1f5a1dcd9cca90410b3cd5d46051
    SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(odysseus_ts_runtime odysseus_ts_c_source odysseus_ts_cpp_source)

add_library(odysseus_tree_sitter STATIC ${odysseus_ts_runtime_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(odysseus_tree_sitter PUBLIC ${odysseus_ts_runtime_SOURCE_DIR}/lib/include
    PRIVATE ${odysseus_ts_runtime_SOURCE_DIR}/lib/src)
add_library(odysseus_ts_c STATIC ${odysseus_ts_c_source_SOURCE_DIR}/src/parser.c)
target_include_directories(odysseus_ts_c PRIVATE ${odysseus_ts_c_source_SOURCE_DIR}/src)
add_library(odysseus_ts_cpp STATIC ${odysseus_ts_cpp_source_SOURCE_DIR}/src/parser.c
    ${odysseus_ts_cpp_source_SOURCE_DIR}/src/scanner.c)
target_include_directories(odysseus_ts_cpp PRIVATE ${odysseus_ts_cpp_source_SOURCE_DIR}/src)
foreach(target odysseus_tree_sitter odysseus_ts_c odysseus_ts_cpp)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES POSITION_INDEPENDENT_CODE YES)
endforeach()
configure_file(${CMAKE_CURRENT_LIST_DIR}/tree_sitter_versions.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/tree_sitter_versions.hpp @ONLY)
