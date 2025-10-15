cmake_minimum_required(VERSION 3.25)

project(drmtest LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)

find_package(OpenGL REQUIRED COMPONENTS EGL GLES2)
#ldrm -lgbm -lEGL -lGLESv2 -I/usr/include/libdrm -I/usr/include/GLES2

message(STATUS "libcamera library found - version: ${LIBCAMERA_VERSION} - libraries: ${LIBCAMERA_LINK_LIBRARIES} - include path: ${LIBCAMERA_INCLUDE_DIRS}")
message(STATUS "libdrm library found - version: ${DRM_VERSION} - libraries: ${DRM_LINK_LIBRARIES} - include path: ${DRM_INCLUDE_DIRS}")


add_executable(drmtest test.c)
target_include_directories(drmtest PRIVATE ${DRM_INCLUDE_DIRS})
target_link_libraries(drmtest PRIVATE ${DRM_LIBRARIES})

add_executable(rect rect.c)
target_include_directories(rect PRIVATE ${DRM_INCLUDE_DIRS})
target_link_libraries(rect PRIVATE ${DRM_LIBRARIES})

add_executable(modeset modeset.c)
target_include_directories(modeset PRIVATE ${DRM_INCLUDE_DIRS} ${LIBCAMERA_INCLUDE_DIRS})
target_link_libraries(modeset PRIVATE ${DRM_LIBRARIES} ${LIBCAMERA_LINK_LIBRARIES})

add_executable(camdrm camdrm.cpp)
target_include_directories(camdrm PRIVATE ${DRM_INCLUDE_DIRS} ${LIBCAMERA_INCLUDE_DIRS})
target_link_libraries(camdrm PRIVATE ${DRM_LIBRARIES} ${LIBCAMERA_LINK_LIBRARIES})


