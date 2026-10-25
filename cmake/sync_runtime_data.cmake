# Seed saves from Debug for other configurations; never overwrite progress.
#   cmake -DSRC=<build>/Debug -DDST=<exe dir> -P sync_runtime_data.cmake
if(SRC STREQUAL DST)
    return()
endif()
# saves/: only seeded (missing files), never overwritten -- each exe keeps its own progress afterwards
file(GLOB saves RELATIVE "${SRC}/saves" "${SRC}/saves/*")
foreach(s IN LISTS saves)
    if(NOT EXISTS "${DST}/saves/${s}")
        file(COPY "${SRC}/saves/${s}" DESTINATION "${DST}/saves")
    endif()
endforeach()
