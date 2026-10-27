# Pin the SDK and verify its archive. Only headers and the static loader are
# used; the Evergreen runtime stays an optional machine prerequisite.
include(FetchContent)
FetchContent_Declare(appx_webview2
  URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.2903.40/microsoft.web.webview2.1.0.2903.40.nupkg
  URL_HASH SHA256=ef128016dd1e51c59178c827ed5b8aa3322c57afa8675d930f8109505542ad74
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(appx_webview2)
target_include_directories(appxportable PRIVATE "${appx_webview2_SOURCE_DIR}/build/native/include")
target_link_libraries(appxportable PRIVATE
  "${appx_webview2_SOURCE_DIR}/build/native/${APPX_HOST_ARCH}/WebView2LoaderStatic.lib" version)
configure_file("${CMAKE_SOURCE_DIR}/docs/WebView2-LICENSE.txt" "${CMAKE_BINARY_DIR}/WebView2-LICENSE.txt" COPYONLY)
add_custom_command(TARGET appxportable POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/docs/WebView2-LICENSE.txt" "$<TARGET_FILE_DIR:appxportable>/WebView2-LICENSE.txt")
