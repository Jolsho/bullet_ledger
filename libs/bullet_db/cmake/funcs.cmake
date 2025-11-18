# SPDX-License-Identifier: GPL-3.0-only

# cmake/functions.cmake
function(prefix_list out_var prefix)
    set(suffix ".cpp")
    set(basenames "${ARGN}")

    set(result "")
    foreach(item IN LISTS basenames)
        list(APPEND result "${prefix}${item}${suffix}")
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()
