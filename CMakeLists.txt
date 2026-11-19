cmake_minimum_required(VERSION 3.25)
project(FeinStatic VERSION 2.1.0 LANGUAGES CXX)

option(FEINSTATIC_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FEINSTATIC_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer for concurrency/race validation" OFF)
option(FEINSTATIC_BUILD_TESTS "Build the test suite" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto SSL)
find_package(SQLite3 3.24 REQUIRED)
find_package(Boost 1.74 CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# Generate protobuf C++ sources from .proto definitions at build time. This
# ensures compatibility with the protobuf version installed on the build host
# (Ubuntu 24.04 provides protobuf 3.21.12; pre-generated sources from a newer
# protoc would fail with header incompatibilities).
protobuf_generate(
    OUT_VAR PROTO_SRCS
    LANGUAGE cpp
    PROTOS
        proto/tendermint/abci/types.proto
        proto/tendermint/crypto/keys.proto
        proto/tendermint/crypto/proof.proto
        proto/tendermint/types/params.proto
        proto/tendermint/types/validator.proto
        proto/gogoproto/gogo.proto
        proto/google/protobuf/timestamp.proto
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/proto
)

# CMake 4.x renamed the imported target; retain compatibility with CMake 3.25+.
if(TARGET SQLite3::SQLite3)
    set(FEINSTATIC_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(FEINSTATIC_SQLITE_TARGET SQLite::SQLite3)
endif()

add_library(feinstatic_core
    src/crypto/crypto.cpp
    src/ledger/wallet.cpp
    src/ledger/transaction.cpp
    src/ledger/block.cpp
    src/ledger/chain.cpp
    src/ledger/mempool.cpp
    src/ledger/ledger.cpp
    src/storage/sqlite_store.cpp
    src/settlement/settlement_book.cpp
    src/platform/financial_platform.cpp
    src/bridge/bridge_service.cpp
    src/network/node.cpp
    src/network/peer_lifecycle.cpp
    src/network/peer_identity.cpp
    src/network/identity.cpp
    src/network/node_record.cpp
    src/network/peer_store.cpp
    src/network/bootstrap.cpp
    src/network/peer_exchange.cpp
    src/network/secure_session.cpp
    src/network/peer_session.cpp
    src/network/peer_admission.cpp
    src/network/message_dispatch.cpp
    src/network/network_runtime.cpp
    src/consensus/fork_choice.cpp
    src/consensus/raft.cpp
    src/consensus/raft_membership.cpp
    src/consensus/raft_storage.cpp
    src/consensus/raft_ledger_adapter.cpp
    src/monetary/engine.cpp
    src/consensus/abci/abci_codec.cpp
    src/consensus/abci/abci_socket_server.cpp
    src/consensus/abci/fst_application.cpp
    src/consensus/abci/gossip.cpp
    src/consensus/abci/rate_limiter.cpp
    src/consensus/abci/validator_peer_set.cpp
    ${PROTO_SRCS}
    src/staking/state.cpp
    src/utxo/encoding.cpp
    src/utxo/validation.cpp
    src/utxo/utxo_set.cpp
    src/utxo/mempool.cpp
    src/exchange/matching_engine.cpp
    src/flow/amm.cpp
    src/application/amount.cpp
    src/application/terminal.cpp
    src/application/verification.cpp
)
add_library(FeinStatic::Core ALIAS feinstatic_core)

target_include_directories(feinstatic_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/proto>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(feinstatic_core PUBLIC OpenSSL::Crypto OpenSSL::SSL ${FEINSTATIC_SQLITE_TARGET} Threads::Threads protobuf absl_strings absl_strerror absl_base absl_synchronization absl_stacktrace absl_symbolize absl_time absl_civil_time absl_time_zone)

# Link against all available Abseil shared libraries on the build host.
# Ubuntu 24.04's libabsl-dev provides a subset of Abseil libraries; globbing
# ensures we link against whatever is available without hardcoding names.
file(GLOB ABSL_LIBS "/usr/lib/libabsl*.so" "/usr/lib/x86_64-linux-gnu/libabsl*.so")
foreach(ABSL_LIB ${ABSL_LIBS})
    get_filename_component(ABSL_NAME ${ABSL_LIB} NAME_WE)
    string(REGEX REPLACE "^lib" "" ABSL_NAME ${ABSL_NAME})
    list(APPEND ABSL_LIB_NAMES ${ABSL_NAME})
endforeach()
if(ABSL_LIB_NAMES)
    list(REMOVE_DUPLICATES ABSL_LIB_NAMES)
    target_link_libraries(feinstatic_core PUBLIC ${ABSL_LIB_NAMES})
endif()
target_include_directories(feinstatic_core SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
# Compile against the non-deprecated Boost.Asio surface to catch API removals early.
target_compile_definitions(feinstatic_core PUBLIC BOOST_ASIO_NO_DEPRECATED)
target_compile_features(feinstatic_core PUBLIC cxx_std_20)
target_compile_options(feinstatic_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Wnon-virtual-dtor>
)

if(FEINSTATIC_ENABLE_SANITIZERS AND FEINSTATIC_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "Address/Undefined sanitizers and ThreadSanitizer must use separate build directories")
endif()

if(FEINSTATIC_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(feinstatic_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(feinstatic_core PUBLIC -fsanitize=address,undefined)
endif()

if(FEINSTATIC_ENABLE_THREAD_SANITIZER)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(feinstatic_core PUBLIC -fsanitize=thread -fno-omit-frame-pointer -g)
        target_link_options(feinstatic_core PUBLIC -fsanitize=thread)
    else()
        message(FATAL_ERROR "ThreadSanitizer is supported only with GCC or Clang")
    endif()
endif()

add_executable(feinstatic apps/feinstatic-tui/main.cpp)
target_link_libraries(feinstatic PRIVATE FeinStatic::Core)
target_compile_options(feinstatic PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

if(FEINSTATIC_BUILD_TESTS)
    enable_testing()
    add_executable(feinstatic_tests
        tests/test_main.cpp
        tests/abci_tests.cpp
        tests/cometbft_integration_tests.cpp
        tests/crypto_tests.cpp
        tests/ledger_tests.cpp
        tests/storage_tests.cpp
        tests/application_tests.cpp
        tests/exchange_tests.cpp
        tests/flow_tests.cpp
        tests/monetary_tests.cpp
        tests/staking_tests.cpp
        tests/terminal_tests.cpp
        tests/utxo_tests.cpp
        tests/platform_tests.cpp
        tests/bridge_tests.cpp
        tests/network_tests.cpp
        tests/peer_lifecycle_tests.cpp
        tests/consensus_tests.cpp
        tests/raft_tests.cpp
        tests/raft_membership_tests.cpp
        tests/raft_ledger_tests.cpp
        tests/mempool_tests.cpp
        tests/identity_tests.cpp
        tests/node_record_tests.cpp
        tests/peer_store_tests.cpp
        tests/bootstrap_tests.cpp
        tests/peer_exchange_tests.cpp
        tests/network_discovery_tests.cpp
        tests/secure_session_tests.cpp
        tests/peer_admission_tests.cpp
        tests/message_dispatch_tests.cpp
        tests/network_integration_tests.cpp
        tests/network_harness.cpp
        tests/network_harness_tests.cpp
        tests/temp_handshake_test.cpp
    )
    target_link_libraries(feinstatic_tests PRIVATE FeinStatic::Core)
    add_dependencies(feinstatic_tests feinstatic)
    target_compile_definitions(feinstatic_tests PRIVATE
        FEINSTATIC_TEST_EXECUTABLE="$<TARGET_FILE:feinstatic>"
    )
    target_compile_options(feinstatic_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
    )
    add_test(NAME feinstatic_tests COMMAND feinstatic_tests)
    set_tests_properties(feinstatic_tests PROPERTIES TIMEOUT 120)
endif()

add_executable(feinstatic_raft_lab_prepare tools/raft_lab_prepare.cpp)
target_link_libraries(feinstatic_raft_lab_prepare PRIVATE FeinStatic::Core)
target_compile_options(feinstatic_raft_lab_prepare PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

add_executable(feinstatic_bench benchmarks/match_benchmark.cpp)
target_link_libraries(feinstatic_bench PRIVATE FeinStatic::Core)
target_compile_options(feinstatic_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

add_executable(feinstatic-demo apps/feinstatic-demo/main.cpp)
target_link_libraries(feinstatic-demo PRIVATE FeinStatic::Core)
target_compile_options(feinstatic-demo PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

install(TARGETS feinstatic feinstatic_bench feinstatic-demo RUNTIME DESTINATION bin)
