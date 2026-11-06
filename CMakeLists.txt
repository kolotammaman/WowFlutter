cmake_minimum_required(VERSION 3.21)
project(WowFlutter VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE — clone into a sibling 'JUCE' folder:
#   git clone https://github.com/juce-framework/JUCE.git JUCE
# ---------------------------------------------------------------------------
add_subdirectory(JUCE)

juce_add_plugin(WowFlutter
    COMPANY_NAME                "madebymolo"
    PLUGIN_MANUFACTURER_CODE    Mycm        # 4-char code, change to your own
    PLUGIN_CODE                 Wflt        # 4-char unique code
    FORMATS                     AU VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE        # copies to ~/Library/Audio/Plug-Ins/Components/
    PRODUCT_NAME                "WowFlutter"
)

juce_generate_juce_header(WowFlutter)

target_sources(WowFlutter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(WowFlutter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(WowFlutter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
