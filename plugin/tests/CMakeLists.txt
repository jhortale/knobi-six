# SPDX-License-Identifier: GPL-3.0-only
# The plugin's tests (CTest): test_plugin.cpp (the processor without a host; plan Task 3), each a CTest of its own;
# bundle_exports (the bundles export no firmware / core symbol); auval (only with -DKNOBI_TEST_AUVAL=ON: it needs the
# AU registered, i.e. installed in ~/Library/Audio/Plug-Ins/Components: CI does that, plugin/README.md says how).
add_library(alloc_hook SHARED alloc_hook.c)        # (dyld interposing: a dylib loaded at launch)
set_target_properties(alloc_hook PROPERTIES C_STANDARD 11 C_VISIBILITY_PRESET default)

add_executable(test_plugin test_plugin.cpp)
# the shared code (KnobiSix: the processor + JUCE, compiled once) and its headers and definitions, not JUCE's sources again
target_link_libraries(test_plugin PRIVATE KnobiSix alloc_hook)
target_include_directories(test_plugin PRIVATE $<TARGET_PROPERTY:KnobiSix,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_plugin PRIVATE $<TARGET_PROPERTY:KnobiSix,COMPILE_DEFINITIONS>)
# factory_gating: the bundles' names as this build makes them
target_compile_definitions(test_plugin PRIVATE "KNOBI_AU_BUNDLE=\"$<TARGET_BUNDLE_DIR:KnobiSix_AU>\""
                                               "KNOBI_VST3_BUNDLE=\"$<TARGET_BUNDLE_DIR:KnobiSix_VST3>\"")
target_compile_options(test_plugin PRIVATE -O2 -Wno-float-equal -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-conversion)

foreach(t render_44k_bitexact render_48k midi_sample_accurate rt_no_alloc seam silent_block prepare_keeps_pending
        events_ordered params_layout automation_to_core core_to_host state_roundtrip state_bank_changed
        state_versioned rt_params transport_start_bar transport_aligned transport_loop transport_loop_tempo transport_locate
        transport_tempo_ramp transport_stop_internal bpm_param_greyed
        library_roundtrip library_atomic library_two_writers library_corrupt reload_off_audio_thread
        library_snapshot_consistent library_write_restore library_globals factory_gating
        library_write_plays_on library_symlink library_globals_order)
    add_test(NAME ${t} COMMAND test_plugin ${t})
endforeach()

add_test(NAME bundle_exports
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.sh" "$<TARGET_FILE:knobi_core>"
            "$<TARGET_FILE:KnobiSix_AU>" "$<TARGET_FILE:KnobiSix_VST3>")

option(KNOBI_TEST_AUVAL "Run auval on the installed AU (aumu Kn6x Jhrt) as a test" OFF)
if(KNOBI_TEST_AUVAL)
    add_test(NAME auval COMMAND auval -strict -v aumu ${KNOBI_PLUGIN_CODE} Jhrt)
endif()
