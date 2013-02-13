set(CMAKE_SHARED_MODULE_SUFFIX ".vis")
set(CMAKE_SHARED_MODULE_PREFIX "")

add_subdirectory(WaveForm)

if(NOT WIN32)
    include_directories(
      ${root}/xbmc/visualizations/EGLHelpers
      )

  add_subdirectory(EGLHelpers)
  add_subdirectory(OpenGLSpectrum)
#  add_subdirectory(XBMCProjectM)
  
  if (NOT APPLE)
    add_subdirectory(Goom)
  endif()
else()
  add_subdirectory(Milkdrop)
  add_subdirectory(DirectXSpectrum)
endif()
