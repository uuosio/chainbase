add_executable( chainbase_test
    test.cpp
    undo_index.cpp
    undo_index_ex.cpp
    grow_shrink.cpp
)

target_link_libraries( chainbase_test  chainbase Boost::unit_test_framework ${PLATFORM_SPECIFIC_LIBS} )

add_test(chainbase_test chainbase_test)
