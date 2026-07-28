# Resolve the current git commit and configure BuildInfo.h. Run at BUILD time (not just
# configure) via a custom target, so the baked-in SHA the update check compares against is
# always the commit actually being built. Inputs (passed with -D):
#   SRC_DIR  : the repo working directory to query
#   IN_FILE  : BuildInfo.h.in template
#   OUT_FILE : destination header
find_package(Git QUIET)

set(SE_BUILD_COMMIT "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE SE_BUILD_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(SE_BUILD_COMMIT STREQUAL "")
        set(SE_BUILD_COMMIT "unknown")
    endif()

    # Mark a dirty working tree so a locally-modified build never claims to match a clean
    # upstream commit.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE SE_GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

if(SE_BUILD_COMMIT STREQUAL "unknown")
    set(SE_BUILD_COMMIT_SHORT "unknown")
else()
    string(SUBSTRING "${SE_BUILD_COMMIT}" 0 10 SE_BUILD_COMMIT_SHORT)
    if(NOT SE_GIT_DIRTY STREQUAL "")
        set(SE_BUILD_COMMIT "${SE_BUILD_COMMIT}-dirty")
        set(SE_BUILD_COMMIT_SHORT "${SE_BUILD_COMMIT_SHORT}-dirty")
    endif()
endif()

# Only rewrite when the contents change, so an unchanged SHA doesn't force a rebuild.
configure_file("${IN_FILE}" "${OUT_FILE}.tmp" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${OUT_FILE}.tmp" "${OUT_FILE}")
file(REMOVE "${OUT_FILE}.tmp")
