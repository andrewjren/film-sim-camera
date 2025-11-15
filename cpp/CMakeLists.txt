cmake_minimum_required(VERSION 3.25)

project(drmtest LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)
pkg_check_modules(GBM REQUIRED gbm)

find_package(OpenGL REQUIRED COMPONENTS EGL GLES2 GLES3)
#ldrm -lgbm -lEGL -lGLESv2 -I/usr/include/libdrm -I/usr/include/GLES2
#find_package(glm CONFIG REQUIRED)

message(STATUS "libcamera library found - version: ${LIBCAMERA_VERSION} - libraries: ${LIBCAMERA_LINK_LIBRARIES} - include path: ${LIBCAMERA_INCLUDE_DIRS}")
message(STATUS "libdrm library found - version: ${DRM_VERSION} - libraries: ${DRM_LINK_LIBRARIES} - include path: ${DRM_INCLUDE_DIRS}")
message(STATUS "gbm library found - version: ${GBM_VERSION} - libraries: ${GBM_LINK_LIBRARIES} - include path: ${GBM_INCLUDE_DIRS}")

add_library(libpicamera STATIC PiCamera.cpp)
target_include_directories(libpicamera PUBLIC
  ${LIBCAMERA_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
  )
target_link_libraries(libpicamera PUBLIC 
  ${LIBCAMERA_LINK_LIBRARIES}
  )

add_executable(camdrm camdrm.cpp dma_heaps.cpp)
target_include_directories(camdrm PRIVATE 
  ${DRM_INCLUDE_DIRS} 
  #${LIBCAMERA_INCLUDE_DIRS} 
  ${OPENGL_EGL_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIRS}
  ${GBM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
  )

target_link_libraries(camdrm PRIVATE 
  ${DRM_LINK_LIBRARIES} 
  #${LIBCAMERA_LINK_LIBRARIES} 
  OpenGL::OpenGL 
  OpenGL::EGL 
  OpenGL::GLES3
  ${GBM_LINK_LIBRARIES}
  #glm::glm-header-only
  libpicamera
  )

