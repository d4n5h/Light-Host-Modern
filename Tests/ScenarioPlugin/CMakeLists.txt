# A loadable simulated processor for large-chain validation. This directory is
# excluded from the default build and its artifact is outside the host payload.
juce_add_plugin(LHFixture
    COMPANY_NAME "LightHostModern Tests"
    BUNDLE_ID "com.lighthost.tests.scenario"
    PLUGIN_MANUFACTURER_CODE LhTs
    PLUGIN_CODE ScnR
    FORMATS VST3
    # JUCE 8.0.13's manifest helper hardcodes its bundled SDK while this host
    # uses an external SDK. The fixture needs live verified metadata only.
    VST3_AUTO_MANIFEST FALSE
    PRODUCT_NAME "LightHostModern Scenario Fixture"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx)
target_sources(LHFixture PRIVATE ScenarioProcessor.cpp)
target_compile_definitions(LHFixture PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(LHFixture PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
