juce_add_gui_app(TickerBar PRODUCT_NAME "TickerBar" VERSION "0.2.1"
    COMPANY_NAME "TickerBar" BUNDLE_ID "com.tickerbar.app"
    ICON_BIG "icon/icon_big.png" ICON_SMALL "icon/icon_small.png")
target_sources(TickerBar PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/PriceClient.cpp
    Source/ContextClient.cpp
    Source/ScreenSpace.cpp
    Source/ToastNotifier.cpp)
target_include_directories(TickerBar PRIVATE Source ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
target_compile_definitions(TickerBar PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="TickerBar"
    JUCE_APPLICATION_VERSION_STRING="0.2.1"
    # NOMINMAX (MeterBar v0.12.1 lesson): windows.h defines min/max as
    # macros that mangle std::min/std::max call sites on MSVC.
    NOMINMAX)
target_link_libraries(TickerBar PRIVATE
    juce::juce_gui_basics juce::juce_gui_extra
    juce::juce_audio_basics juce::juce_audio_devices
    tickerbar_core juce::juce_recommended_config_flags)
if(WIN32)
  # shell32 = SHAppBarMessage, comctl32 = SetWindowSubclass,
  # dwmapi = DWMWA_CLOAKED — all for ScreenSpace (AppBar/arrange).
  target_link_libraries(TickerBar PRIVATE shell32 comctl32 dwmapi)
endif()
