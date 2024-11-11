include(ExternalProject)

ExternalProject_Add(
    openssl
    PREFIX ${CMAKE_BINARY_DIR}/openssl
    GIT_REPOSITORY https://github.com/openssl/openssl.git
    GIT_TAG "openssl-3.3"
    #CONFIGURE_COMMAND <SOURCE_DIR>/Configure android-arm -D__ANDROID_API__=21 --prefix=<INSTALL_DIR>
    #CONFIGURE_COMMAND PATH=${ANDROID_TOOLCHAIN_DIR}:$ENV{PATH} ./Configure android-arm -D__ANDROID_API__=${ANDROID_API} --prefix=<INSTALL_DIR>
    #CONFIGURE_COMMAND PATH=${ANDROID_TOOLCHAIN_DIR}:$ENV{PATH} <SOURCE_DIR>/Configure android-arm -D__ANDROID_API__=26 --prefix=<INSTALL_DIR>

    #CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-fPIC <SOURCE_DIR>/Configure android-arm -D__ANDROID_API__=23 --prefix=<INSTALL_DIR>

    CONFIGURE_COMMAND
        <SOURCE_DIR>/Configure android-arm -D__ANDROID_API__=21 --prefix=<INSTALL_DIR>

    BUILD_COMMAND make
    INSTALL_COMMAND make install
)

ExternalProject_Get_Property(openssl INSTALL_DIR)
set(OPENSSL_INCLUDE_DIR ${INSTALL_DIR}/include)
set(OPENSSL_LIB_DIR ${INSTALL_DIR}/lib)
