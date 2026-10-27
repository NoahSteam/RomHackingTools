# Resolve the current git commit and configure BuildInfo.h. Run at BUILD time (not just
# configure) via a custom target, so the baked-in SHA the update check compares against is
# always the commit actually being built. Inputs (passed with -D):
#   SRC_DIR  : the repo working directory to query
#   IN_FILE  : BuildInfo.h.in template
#   OUT_FILE : destination header
find_package(Git QUIET)

set(SE_BUILD_COMMIT "unknown")
set(SE_GIT_DIRTY "")
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

# The release this build is, for the update check's comparison with the latest GitHub release: the
# tag the release workflow is building (SE_RELEASE_VERSION in the environment, set from the tag),
# else a v* tag sitting exactly on HEAD. Empty for anything else, which the update check reports as
# "not a tagged release" rather than guessing. SE_RELEASE_VERSION is a statement by whoever runs the
# build (the release workflow) that this IS that release, so it stands even if the tree reads as
# modified; a tag merely found on HEAD does not count for a modified tree. Only a filename-safe name
# is baked in because it ends up inside a C++ string literal.
set(SE_BUILD_VERSION "")
set(SE_VERSION_EXPLICIT FALSE)
if(DEFINED ENV{SE_RELEASE_VERSION} AND NOT "$ENV{SE_RELEASE_VERSION}" STREQUAL "")
    set(SE_BUILD_VERSION "$ENV{SE_RELEASE_VERSION}")
    set(SE_VERSION_EXPLICIT TRUE)
elseif(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --match "v[0-9]*" HEAD
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE SE_BUILD_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE SE_DESCRIBE_RESULT
        ERROR_QUIET)
    if(NOT SE_DESCRIBE_RESULT EQUAL 0)
        set(SE_BUILD_VERSION "")
    endif()
endif()
if(NOT SE_BUILD_VERSION MATCHES "^[A-Za-z0-9._+-]+$")
    set(SE_BUILD_VERSION "")
elseif(NOT SE_VERSION_EXPLICIT AND NOT SE_GIT_DIRTY STREQUAL "")
    set(SE_BUILD_VERSION "")
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
