# Loaded by CTest after the generated GoogleTest registrations.
foreach(suite IN ITEMS arguments format_hash codec)
    if(suite STREQUAL "arguments")
        set(target_name qlog_argument_model_test)
    elseif(suite STREQUAL "format_hash")
        set(target_name qlog_format_hash_test)
    else()
        set(target_name qlog_record_codec_test)
    endif()
    if(DEFINED ${target_name}_TESTS)
        set_tests_properties(${${target_name}_TESTS}
            PROPERTIES LABELS "record_core;${suite}")
    endif()
endforeach()
