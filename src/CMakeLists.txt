if(WIN32)
  set(PLATFORM_SOURCES audio/mic-capture-win.cc nlu/ws-client-win.cc audio/audio-play-win.cc)
  set(PLATFORM_LIBS ole32 uuid winhttp)
elseif(APPLE)
  set(PLATFORM_SOURCES audio/mic-capture-mac.cc nlu/ws-client-posix.cc audio/audio-play-mac.cc)
  set(PLATFORM_LIBS)
else()
  set(PLATFORM_SOURCES audio/mic-capture-linux.cc nlu/ws-client-posix.cc audio/audio-play-linux.cc)
  set(PLATFORM_LIBS)
endif()

add_executable(voice-assistant
  voice_assistant.cc
  vad/vad-module.cc
  kws/kws-module.cc
  asr/asr-module.cc
  nlu/nlu-module.cc
  tts/tts-module.cc
  common/json-config.cc
  ${PLATFORM_SOURCES}
)
target_include_directories(voice-assistant PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/sherpa-onnx
)
target_link_libraries(voice-assistant sherpa-onnx-core ${PLATFORM_LIBS})
if(MSVC)
  target_compile_options(voice-assistant PRIVATE /utf-8)
endif()
install(TARGETS voice-assistant DESTINATION bin)

add_executable(tts-test
  tts-test.cc
)
target_include_directories(tts-test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/sherpa-onnx
)
target_link_libraries(tts-test sherpa-onnx-core ${PLATFORM_LIBS})
if(MSVC)
  target_compile_options(tts-test PRIVATE /utf-8)
endif()
install(TARGETS tts-test DESTINATION bin)
