cmake_minimum_required(VERSION 3.20)
project(DeepGlow2 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS: ship a universal plugin, which is what Resolve expects on both Intel
# and Apple silicon.
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# ---------------------------------------------------------------------------
# OpenFX SDK / OFX Support library.
#
# Two layouts are accepted, so the same tree builds against either the SDK that
# ships with DaVinci Resolve or the upstream openfx repository:
#
#   Resolve SDK   <root>/OpenFX-1.4/include   <root>/Support/{include,Library}
#   openfx repo   <root>/include              <root>/Support/{include,Library}
#
# Point OFX_ROOT at the directory that contains Support/.  CI clones the
# upstream repository and passes -DOFX_ROOT=<clone>.
# ---------------------------------------------------------------------------
if(NOT DEFINED OFX_ROOT)
  if(APPLE)
    set(_dg_ofx_default
        "/Library/Application Support/Blackmagic Design/DaVinci Resolve/Developer/OpenFX")
  elseif(WIN32)
    set(_dg_ofx_default
        "C:/ProgramData/Blackmagic Design/DaVinci Resolve/Support/Developer/OpenFX")
  else()
    set(_dg_ofx_default "/opt/resolve/Developer/OpenFX")
  endif()
  set(OFX_ROOT "${_dg_ofx_default}" CACHE PATH
      "Root of the OpenFX SDK (contains Support/ and the OFX headers)")
endif()

set(OFX_INCLUDE "")
foreach(_candidate "${OFX_ROOT}/OpenFX-1.4/include" "${OFX_ROOT}/include")
  if(EXISTS "${_candidate}/ofxImageEffect.h")
    set(OFX_INCLUDE "${_candidate}")
    break()
  endif()
endforeach()

set(OFX_SUPPORT_INCLUDE "${OFX_ROOT}/Support/include")
set(OFX_SUPPORT_LIB     "${OFX_ROOT}/Support/Library")

if(NOT OFX_INCLUDE)
  message(FATAL_ERROR
    "OpenFX headers not found under '${OFX_ROOT}'.\n"
    "Looked in '${OFX_ROOT}/OpenFX-1.4/include' and '${OFX_ROOT}/include'.\n"
    "Configure with -DOFX_ROOT=<path to .../Developer/OpenFX>.")
endif()
if(NOT EXISTS "${OFX_SUPPORT_LIB}/ofxsImageEffect.cpp")
  message(FATAL_ERROR
    "OFX Support sources not found at '${OFX_SUPPORT_LIB}'.\n"
    "Configure with -DOFX_ROOT=<path to .../Developer/OpenFX>.")
endif()

# The DaVinci Resolve SDK extends the OFX Support library with a handful of
# hooks that upstream openfx does not ship.  Detect them rather than pinning a
# version, so one tree builds against either.
file(STRINGS "${OFX_SUPPORT_INCLUDE}/ofxsImageEffect.h" _dg_nsa
     REGEX "setNoSpatialAwareness")
if(_dg_nsa)
  set(DG_OFX_RESOLVE_EXTENSIONS ON)
endif()

set(OFX_SUPPORT_SOURCES "")
# The upstream SDK and the Resolve build of it differ slightly in which files
# they ship, so only add what is actually there.
foreach(_src
    ofxsCore.cpp
    ofxsImageEffect.cpp
    ofxsInteract.cpp
    ofxsLog.cpp
    ofxsMultiThread.cpp
    ofxsParams.cpp
    ofxsProperty.cpp
    ofxsPropertyValidation.cpp)
  if(EXISTS "${OFX_SUPPORT_LIB}/${_src}")
    list(APPEND OFX_SUPPORT_SOURCES "${OFX_SUPPORT_LIB}/${_src}")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# the plugin itself
# ---------------------------------------------------------------------------
add_library(DeepGlow2 SHARED
  src/DeepGlowOFX.cpp
  src/DGPipeline.cpp
  ${OFX_SUPPORT_SOURCES}
)

target_include_directories(DeepGlow2 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${OFX_INCLUDE}"
  "${OFX_SUPPORT_INCLUDE}"
)

if(DG_OFX_RESOLVE_EXTENSIONS)
  target_compile_definitions(DeepGlow2 PRIVATE DG_OFX_HAS_NO_SPATIAL_AWARENESS)
endif()

set_target_properties(DeepGlow2 PROPERTIES
  PREFIX ""
  SUFFIX ".ofx"
  OUTPUT_NAME "DeepGlow2"
)

if(MSVC)
  set_property(TARGET DeepGlow2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  target_compile_options(DeepGlow2 PRIVATE /W3 /permissive /MP /fp:precise /EHsc)
  target_compile_definitions(DeepGlow2 PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(DeepGlow2 PRIVATE -O3 -fvisibility=hidden -Wall -Wextra
                                           -Wno-unused-parameter)
endif()

if(APPLE)
  # OFX plugins are dlopen()ed bundles: host symbols are resolved at load time.
  set_property(TARGET DeepGlow2 APPEND_STRING PROPERTY LINK_FLAGS " -undefined dynamic_lookup")
endif()

# The support sources are third party; do not bury the log in their warnings.
if(MSVC)
  set_source_files_properties(${OFX_SUPPORT_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(${OFX_SUPPORT_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# ---------------------------------------------------------------------------
# .ofx.bundle layout that Resolve picks up from its OFX search paths
# ---------------------------------------------------------------------------
if(WIN32)
  set(DG_BUNDLE_ARCH "Win64")
elseif(APPLE)
  set(DG_BUNDLE_ARCH "MacOS")
else()
  set(DG_BUNDLE_ARCH "Linux-x86-64")
endif()
set(DG_BUNDLE_DIR "${CMAKE_BINARY_DIR}/bundle/DeepGlow2.ofx.bundle")

if(APPLE)
  file(WRITE "${CMAKE_BINARY_DIR}/bundle/Info.plist.in"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
  <key>CFBundleExecutable</key><string>DeepGlow2.ofx</string>
  <key>CFBundleIdentifier</key><string>com.deepglow.ofx.DeepGlow2</string>
  <key>CFBundleName</key><string>Deep Glow 2</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>@PROJECT_VERSION@</string>
  <key>CFBundleVersion</key><string>@PROJECT_VERSION@</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
")
  configure_file("${CMAKE_BINARY_DIR}/bundle/Info.plist.in"
                 "${CMAKE_BINARY_DIR}/bundle/Info.plist" @ONLY)
endif()

add_custom_command(TARGET DeepGlow2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${DG_BUNDLE_DIR}/Contents/${DG_BUNDLE_ARCH}"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${DG_BUNDLE_DIR}/Contents/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:DeepGlow2>"
          "${DG_BUNDLE_DIR}/Contents/${DG_BUNDLE_ARCH}/DeepGlow2.ofx"
  COMMENT "Creating DeepGlow2.ofx.bundle (${DG_BUNDLE_ARCH})")

if(APPLE)
  add_custom_command(TARGET DeepGlow2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/bundle/Info.plist"
            "${DG_BUNDLE_DIR}/Contents/Info.plist"
    COMMENT "Adding Info.plist to the macOS bundle")
endif()

# ---------------------------------------------------------------------------
# headless smoke test for the pixel pipeline (no host required)
# ---------------------------------------------------------------------------
add_executable(DGSmoke tests/smoke.cpp src/DGPipeline.cpp)
target_include_directories(DGSmoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
  set_property(TARGET DGSmoke PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  target_compile_options(DGSmoke PRIVATE /W3 /EHsc /fp:precise)
  target_compile_definitions(DGSmoke PRIVATE NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(DGSmoke PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# in-process mini OFX host: validates the plugin's registration surface
# (describe / describeInContext / createInstance / clip prefs / RoI) without
# needing DaVinci Resolve.  Rebuilds the plugin sources so OfxGetPlugin is in
# the same binary, exactly as the private OFX entry points expect.
# ---------------------------------------------------------------------------
option(DG_VALIDATION
  "Compile the OFX Support library with DEBUG_BUILD (property validation + exception messages)" OFF)

add_executable(DGHostCheck
  tests/hostcheck.cpp
  src/DeepGlowOFX.cpp
  src/DGPipeline.cpp
  ${OFX_SUPPORT_SOURCES}
)
target_include_directories(DGHostCheck PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${OFX_INCLUDE}"
  "${OFX_SUPPORT_INCLUDE}"
)
if(MSVC)
  set_property(TARGET DGHostCheck PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  target_compile_options(DGHostCheck PRIVATE /W3 /permissive /MP /fp:precise /EHsc)
  target_compile_definitions(DGHostCheck PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  # crash locations are printed as exe-relative offsets; the .map resolves them
  target_link_options(DGHostCheck PRIVATE /MAP)
else()
  target_compile_options(DGHostCheck PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)
endif()

if(DG_VALIDATION)
  target_compile_definitions(DGHostCheck PRIVATE DEBUG_BUILD)
endif()
