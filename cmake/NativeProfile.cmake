include(ExternalProject)

if(QA_NATIVE_PLATFORM STREQUAL "linux" AND QA_NATIVE_ARCH STREQUAL "x86_64")
    set(QA_NATIVE_PROFILE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/native-profile")
    set(QA_NATIVE_PROFILE_SDK "${QA_NATIVE_PROFILE_ROOT}/sdk")
    set(QA_NATIVE_PROFILE_CLIENT "${QA_NATIVE_PROFILE_ROOT}/client")
    get_filename_component(QA_NATIVE_PROFILE_SOURCE_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

    ExternalProject_Add(qa-native-profile-sdk
        PREFIX "${QA_NATIVE_PROFILE_ROOT}/sdk-project"
        GIT_REPOSITORY https://github.com/DynamoRIO/dynamorio.git
        GIT_TAG fb2c48b70fc2e070f55ae2d2e7152503568bbf76
        GIT_SHALLOW FALSE
        GIT_SUBMODULES_RECURSE TRUE
        UPDATE_DISCONNECTED TRUE
        CMAKE_ARGS
            "-DCMAKE_INSTALL_PREFIX:PATH=${QA_NATIVE_PROFILE_SDK}"
            "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
            -DCMAKE_BUILD_TYPE:STRING=Release
            -DBUILD_CORE:BOOL=ON
            -DBUILD_TOOLS:BOOL=ON
            -DBUILD_EXT:BOOL=ON
            -DBUILD_CLIENTS:BOOL=OFF
            -DBUILD_SAMPLES:BOOL=OFF
            -DBUILD_DOCS:BOOL=OFF
            -DBUILD_TESTS:BOOL=OFF
            -DDISABLE_DRGUI:BOOL=ON)

    ExternalProject_Add(qa-native-profile-build
        PREFIX "${QA_NATIVE_PROFILE_ROOT}/client-project"
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/native-profile"
        DEPENDS qa-native-profile-sdk
        BUILD_ALWAYS TRUE
        CMAKE_ARGS
            "-DCMAKE_INSTALL_PREFIX:PATH=${QA_NATIVE_PROFILE_CLIENT}"
            "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
            "-DQA_SOURCE_ROOT:PATH=${QA_NATIVE_PROFILE_SOURCE_ROOT}"
            "-DQA_DYNAMORIO_SDK:PATH=${QA_NATIVE_PROFILE_SDK}"
            "-DQA_DECLARED_REGIONS:BOOL=${QA_NATIVE_DYNAMORIO}"
            -DCMAKE_BUILD_TYPE:STRING=Release
        BUILD_BYPRODUCTS "${QA_NATIVE_PROFILE_CLIENT}/qa-native-profile.so")

    add_dependencies(qa-native-runner qa-native-profile-build)
    install(DIRECTORY "${QA_NATIVE_PROFILE_SDK}/"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/dynamorio"
        USE_SOURCE_PERMISSIONS)
    install(FILES "${QA_NATIVE_PROFILE_CLIENT}/qa-native-profile.so"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}")
    if(QA_NATIVE_DYNAMORIO)
        install(FILES "${QA_NATIVE_PROFILE_CLIENT}/qa-native-hooks.so"
            DESTINATION "${QA_NATIVE_INSTALL_DIR}")
    endif()
endif()
