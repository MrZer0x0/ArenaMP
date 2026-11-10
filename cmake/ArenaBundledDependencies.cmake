# Check source bundle integrity before optional dependencies start downloading.
function(arena_check_bundled_sources root)
    set(_required extern/CMakeLists.txt extern/Base64/CMakeLists.txt)
    if(BUILD_OPENMW OR BUILD_OPENCS)
        list(APPEND _required extern/oics/CMakeLists.txt
            extern/osg-ffmpeg-videoplayer/CMakeLists.txt
            extern/maskedoc/CMakeLists.txt extern/maskedoc/MaskedOcclusionCulling.h)
    endif()
    if(BUILD_OPENCS)
        list(APPEND _required extern/osgQt/CMakeLists.txt)
    endif()
    if(BUILD_OPENMW_MP)
        list(APPEND _required extern/LuaBridge/LuaBridge.h extern/PicoSHA2/picosha2.h)
    endif()
    set(_missing "")
    foreach(_file IN LISTS _required)
        if(NOT EXISTS "${root}/${_file}")
            string(APPEND _missing "\n  ${_file}")
        endif()
    endforeach()
    if(_missing)
        message(FATAL_ERROR "ArenaMP bundled source tree is incomplete:${_missing}\n"
            "Restore the complete extern directory from the matching source bundle. "
            "An extern/raknet binary staging folder alone is not sufficient. "
            "See V21_EXTERN_RESTORE_RU.md.")
    endif()
endfunction()
