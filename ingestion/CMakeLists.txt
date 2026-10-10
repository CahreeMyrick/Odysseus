# FetchContent SOURCE_SUBDIR requires CMake 3.18 or newer.
cmake_minimum_required(VERSION 3.18)
# Define the odysseus_ingestion project and enable C++ compilation.
project(odysseus_ingestion LANGUAGES C CXX)

include(cmake/TreeSitter.cmake)

# Defaulted JSON decoding preserves compatibility with legacy model records.
find_package(nlohmann_json 3.11.0 REQUIRED)
# Locate the required PostgreSQL client library and headers.
find_package(PostgreSQL REQUIRED)
# Locate the required curl library and headers.
find_package(CURL REQUIRED)
# Rerun CMake configuration when either SQL schema file changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sql/001_initial.sql sql/002_indexing_tasks.sql)
# Read the initial database schema into INITIAL_SCHEMA_SQL.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sql/001_initial.sql" INITIAL_SCHEMA_SQL)
# Read the indexing schema into INDEXING_SCHEMA_SQL.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sql/002_indexing_tasks.sql" INDEXING_SCHEMA_SQL)
# Append the indexing schema to the initial schema, separated by a newline.
string(APPEND INITIAL_SCHEMA_SQL "\n${INDEXING_SCHEMA_SQL}")
# Generate the schema header from its template, substituting only @VAR@ placeholders.
configure_file(src/schema_sql.hpp.in generated/schema_sql.hpp @ONLY)
# Build the ingestion library from its implementation source files.
add_library(ingestion src/pipeline.cpp src/github_adapter.cpp src/processors.cpp src/storage.cpp src/postgres_storage.cpp src/cli.cpp src/application.cpp src/content_classifier.cpp src/http_transport.cpp src/rest_github_client.cpp src/indexing.cpp src/code_extractor.cpp)
# Require C++17 features for the ingestion library and its consumers.
target_compile_features(ingestion PUBLIC cxx_std_17)
# Expose the ingestion include directory and repository root to the library and its consumers.
target_include_directories(ingestion PUBLIC include ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Add the generated header directory to the ingestion library's private include paths.
target_include_directories(ingestion PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
# Link JSON publicly and PostgreSQL and curl privately to the ingestion library.
target_link_libraries(ingestion PUBLIC nlohmann_json::nlohmann_json PRIVATE PostgreSQL::PostgreSQL CURL::libcurl odysseus_tree_sitter odysseus_ts_c odysseus_ts_cpp)
# Enable additional warnings when compiling with Clang or GNU C++.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Apply standard, extra, and pedantic warnings to the ingestion library.
    target_compile_options(ingestion PRIVATE -Wall -Wextra -Wpedantic)
# End the compiler-specific warning configuration.
endif()
# Build the odysseus command-line executable from its entry point.
add_executable(odysseus src/main.cpp)
# Link the odysseus executable to the ingestion library.
target_link_libraries(odysseus PRIVATE ingestion)
# Build the odysseus-index executable from its indexing entry point.
add_executable(odysseus-index src/index_main.cpp)
# Link the odysseus-index executable to the ingestion library.
target_link_libraries(odysseus-index PRIVATE ingestion)
# Load CTest support and define the BUILD_TESTING option.
include(CTest)
# Create and register tests when BUILD_TESTING is enabled.
if(BUILD_TESTING)
    add_executable(graph_projection_tests tests/graph_projection_test.cpp)
    target_include_directories(graph_projection_tests PRIVATE src)
    target_link_libraries(graph_projection_tests PRIVATE ingestion)
    add_test(NAME graph_projection_contract COMMAND graph_projection_tests)
    add_executable(tree_sitter_tests tests/tree_sitter_test.cpp)
    target_include_directories(tree_sitter_tests PRIVATE src)
    target_link_libraries(tree_sitter_tests PRIVATE ingestion)
    add_test(NAME tree_sitter_contract COMMAND tree_sitter_tests)
    # Build the code extractor test executable.
    add_executable(code_extractor_tests tests/code_extractor_test.cpp)
    # Link the code extractor tests to the ingestion library.
    target_link_libraries(code_extractor_tests PRIVATE ingestion)
    # Register the code extractor tests with CTest.
    add_test(NAME code_extractor COMMAND code_extractor_tests)
    # Build the GitHub-to-PostgreSQL integration test executable.
    add_executable(github_postgres_tests tests/github_postgres_test.cpp)
    # Link the GitHub-to-PostgreSQL tests to ingestion and the PostgreSQL client library.
    target_link_libraries(github_postgres_tests PRIVATE ingestion PostgreSQL::PostgreSQL)
    # Register the GitHub-to-PostgreSQL integration tests with CTest.
    add_test(NAME github_postgres_integration COMMAND github_postgres_tests)
    # Treat exit code 77 as a skipped GitHub-to-PostgreSQL integration test.
    set_tests_properties(github_postgres_integration PROPERTIES SKIP_RETURN_CODE 77)
    # Build the GitHub client test executable.
    add_executable(github_client_tests tests/github_client_test.cpp)
    # Link the GitHub client tests to the ingestion library.
    target_link_libraries(github_client_tests PRIVATE ingestion)
    # Register the GitHub client tests with CTest.
    add_test(NAME github_client COMMAND github_client_tests)
    # Build the PostgreSQL integration test executable.
    add_executable(postgres_tests tests/postgres_test.cpp)
    # Link the PostgreSQL tests to ingestion and the PostgreSQL client library.
    target_link_libraries(postgres_tests PRIVATE ingestion PostgreSQL::PostgreSQL)
    # Register the PostgreSQL integration tests with CTest.
    add_test(NAME postgres_integration COMMAND postgres_tests)
    # Treat exit code 77 as a skipped PostgreSQL integration test.
    set_tests_properties(postgres_integration PROPERTIES SKIP_RETURN_CODE 77)
    # Build the ingestion pipeline test executable.
    add_executable(ingestion_tests tests/pipeline_test.cpp)
    # Link the pipeline tests to the ingestion library.
    target_link_libraries(ingestion_tests PRIVATE ingestion)
    # Register the ingestion pipeline tests with CTest.
    add_test(NAME ingestion_pipeline COMMAND ingestion_tests)
    # Register a CLI test that ingests the example GitHub repository using mock data.
    add_test(NAME mock_repository_cli COMMAND odysseus ingest github https://github.com/example/mock-repo --mock)
    # Require the mock CLI test output to match "Printed 4 artifacts".
    set_tests_properties(mock_repository_cli PROPERTIES PASS_REGULAR_EXPRESSION "Printed 4 artifacts")
# End the optional test configuration.
endif()
