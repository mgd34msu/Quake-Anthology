include(ExternalProject)

if(QA_NATIVE_PLATFORM STREQUAL "linux" AND QA_NATIVE_ARCH STREQUAL "x86_64")
    set(QA_NATIVE_PROFILE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/native-profile")
    set(QA_NATIVE_PROFILE_SDK "${QA_NATIVE_PROFILE_ROOT}/sdk")
    set(QA_NATIVE_PROFILE_CLIENT "${QA_NATIVE_PROFILE_ROOT}/client")
    get_filename_component(QA_NATIVE_PROFILE_SOURCE_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(QA_NATIVE_PROFILE_LINKER_ARGS)
    set(QA_NATIVE_PROFILE_SDK_COMPILER_ARGS)
    set(QA_NATIVE_PROFILE_SDK_ENV)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        get_filename_component(QA_NATIVE_PROFILE_COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        find_program(QA_NATIVE_PROFILE_CXX_COMPILER NAMES clang++
            HINTS "${QA_NATIVE_PROFILE_COMPILER_DIR}" REQUIRED)
        set(QA_NATIVE_PROFILE_SDK_C_FLAGS
            "${CMAKE_C_FLAGS} -fno-builtin -Wno-unknown-warning-option")
        set(QA_NATIVE_PROFILE_SDK_CXX_FLAGS
            "${CMAKE_CXX_FLAGS} -Wno-unknown-warning-option")
        # The pinned SDK rebuilds its core flags from the environment.
        list(APPEND QA_NATIVE_PROFILE_SDK_ENV
            "CFLAGS=${QA_NATIVE_PROFILE_SDK_C_FLAGS}"
            "CXXFLAGS=${QA_NATIVE_PROFILE_SDK_CXX_FLAGS}")
        list(APPEND QA_NATIVE_PROFILE_SDK_COMPILER_ARGS
            "-DCMAKE_CXX_COMPILER:FILEPATH=${QA_NATIVE_PROFILE_CXX_COMPILER}"
            "-DCMAKE_C_FLAGS:STRING=${QA_NATIVE_PROFILE_SDK_C_FLAGS}"
            "-DCMAKE_CXX_FLAGS:STRING=${QA_NATIVE_PROFILE_SDK_CXX_FLAGS}")
        list(APPEND QA_NATIVE_PROFILE_LINKER_ARGS
            "-DCMAKE_LINKER:FILEPATH=${CMAKE_LINKER}"
            "-DCMAKE_EXE_LINKER_FLAGS:STRING=${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=${CMAKE_LINKER}"
            "-DCMAKE_SHARED_LINKER_FLAGS:STRING=${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=${CMAKE_LINKER}"
            "-DCMAKE_MODULE_LINKER_FLAGS:STRING=${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=${CMAKE_LINKER}")
    endif()

    ExternalProject_Add(qa-native-profile-sdk
        PREFIX "${QA_NATIVE_PROFILE_ROOT}/sdk-project"
        GIT_REPOSITORY https://github.com/DynamoRIO/dynamorio.git
        GIT_TAG fb2c48b70fc2e070f55ae2d2e7152503568bbf76
        GIT_SHALLOW FALSE
        GIT_SUBMODULES_RECURSE TRUE
        UPDATE_DISCONNECTED TRUE
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env ${QA_NATIVE_PROFILE_SDK_ENV}
            "${CMAKE_COMMAND}" -S <SOURCE_DIR> -B <BINARY_DIR>
            "-G${CMAKE_GENERATOR}"
            "-DCMAKE_INSTALL_PREFIX:PATH=${QA_NATIVE_PROFILE_SDK}"
            "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
            ${QA_NATIVE_PROFILE_LINKER_ARGS}
            ${QA_NATIVE_PROFILE_SDK_COMPILER_ARGS}
            -DCMAKE_BUILD_TYPE:STRING=Release
            -DBUILD_CORE:BOOL=ON
            -DBUILD_TOOLS:BOOL=ON
            -DBUILD_EXT:BOOL=ON
            -DBUILD_CLIENTS:BOOL=OFF
            -DBUILD_SAMPLES:BOOL=OFF
            -DBUILD_DOCS:BOOL=OFF
            -DBUILD_TESTS:BOOL=OFF
            -DDISABLE_DRGUI:BOOL=ON)

    if(CMAKE_VERSION VERSION_LESS 3.27)
        set(QA_NATIVE_PROFILE_PATCH_DEPENDEE patch)
    else()
        set(QA_NATIVE_PROFILE_PATCH_DEPENDEE patch_disconnected)
    endif()
    ExternalProject_Add_Step(qa-native-profile-sdk app-segment-patch
        COMMAND "${CMAKE_COMMAND}"
            "-DQA_SOURCE_DIR:PATH=<SOURCE_DIR>"
            "-DQA_PATCH:FILEPATH=${CMAKE_CURRENT_LIST_DIR}/native-profile/arch-set-gs.patch"
            -P "${CMAKE_CURRENT_LIST_DIR}/native-profile/ApplyPatch.cmake"
        DEPENDEES ${QA_NATIVE_PROFILE_PATCH_DEPENDEE}
        DEPENDERS configure
        DEPENDS
            "${CMAKE_CURRENT_LIST_DIR}/native-profile/arch-set-gs.patch"
            "${CMAKE_CURRENT_LIST_DIR}/native-profile/ApplyPatch.cmake")

    ExternalProject_Add(qa-native-profile-build
        PREFIX "${QA_NATIVE_PROFILE_ROOT}/client-project"
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/native-profile"
        DEPENDS qa-native-profile-sdk
        BUILD_ALWAYS TRUE
        CMAKE_ARGS
            "-DCMAKE_INSTALL_PREFIX:PATH=${QA_NATIVE_PROFILE_CLIENT}"
            "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
            ${QA_NATIVE_PROFILE_LINKER_ARGS}
            "-DQA_SOURCE_ROOT:PATH=${QA_NATIVE_PROFILE_SOURCE_ROOT}"
            "-DQA_DYNAMORIO_SDK:PATH=${QA_NATIVE_PROFILE_SDK}"
            "-DQA_DECLARED_REGIONS:BOOL=${QA_NATIVE_DYNAMORIO}"
            -DCMAKE_BUILD_TYPE:STRING=Release
        BUILD_BYPRODUCTS "${QA_NATIVE_PROFILE_CLIENT}/qa-native-profile.so")

    add_dependencies(qa-native-runner qa-native-profile-build)
    install(PROGRAMS "${QA_NATIVE_PROFILE_SDK}/bin64/drrun"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/dynamorio/bin64")
    install(PROGRAMS "${QA_NATIVE_PROFILE_SDK}/lib64/release/libdynamorio.so"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/dynamorio/lib64/release")
    install(FILES "${QA_NATIVE_PROFILE_SDK}/lib64/release/libdrpreload.so"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/dynamorio/lib64/release")
    install(FILES
        "${QA_NATIVE_PROFILE_SDK}/ext/lib64/release/libdrmgr.so"
        "${QA_NATIVE_PROFILE_SDK}/ext/lib64/release/libdrutil.so"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/dynamorio/ext/lib64/release")
    install(FILES "${QA_NATIVE_PROFILE_SDK}/License.txt"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/dynamorio")
    install(FILES "${QA_NATIVE_PROFILE_CLIENT}/qa-native-profile.so"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}")
    if(QA_NATIVE_DYNAMORIO)
        install(FILES "${QA_NATIVE_PROFILE_CLIENT}/qa-native-hooks.so"
            DESTINATION "${QA_NATIVE_INSTALL_DIR}")
    endif()
endif()
