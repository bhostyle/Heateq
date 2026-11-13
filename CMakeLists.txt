cmake_minimum_required(VERSION 3.22)
project(HeatEQ VERSION 0.1.0)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(HeatEQ COMPANY_NAME "Respect Heat Holders" BUNDLE_ID com.respectheatholders.heateq
    PLUGIN_MANUFACTURER_CODE Rhhe PLUGIN_CODE Hteq FORMATS VST3 PRODUCT_NAME "Heat EQ"
    VST3_CATEGORIES Fx EQ)
target_sources(HeatEQ PRIVATE Processor.cpp)
target_compile_features(HeatEQ PRIVATE cxx_std_17)
target_compile_definitions(HeatEQ PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(HeatEQ PRIVATE juce::juce_audio_utils juce::juce_dsp)
