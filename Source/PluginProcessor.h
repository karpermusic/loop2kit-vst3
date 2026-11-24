#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Loop2KitPreset.h"
#include <array>
#include <vector>

struct DrumSlice
{
    int index = 0;
    int startSample = 0;
    int endSample = 0;
    int category = 7; // 0=Kick, 1=Rim, 2=Snare, 3=Clap, 4=Tom, 5=ClosedHat, 6=OpenHat, 7=Perc
    int velocity = 100;
    float peak = 0.0f;
};

struct KitPad
{
    int midiNote = 36;
    juce::String name { "Pad" };
    int assignedSlice = -1;
    float gain = 1.0f;
    float pitchSemitones = 0.0f;
    float decay = 1.0f;
    bool muted = false;
};

struct VoiceState
{
    bool active = false;
    double samplePos = 0.0;
    int endSample = 0;
    double rate = 1.0;
    float gain = 1.0f;
    float env = 1.0f;
    float envDecay = 0.9998f;
};

class Loop2KitProcessor final : public juce::AudioProcessor
{
public:
    Loop2KitProcessor();
    ~Loop2KitProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool loadAudioFile(const juce::File& file);
    void analyzeCurrentLoop();
    void triggerPadManual(int padIndex, float velocity = 0.9f);
    bool exportPatternToMidiFile(const juce::File& targetFile) const;

    juce::AudioProcessorValueTreeState apvts;

    const juce::AudioBuffer<float>& getLoopBuffer() const { return loopBuffer; }
    double getLoopSampleRate() const { return loopSampleRate; }
    const std::vector<DrumSlice>& getSlices() const { return slices; }
    const std::array<KitPad, 16>& getPads() const { return pads; }
    const std::vector<Loop2KitPreset::StepEvent>& getPatternEvents() const { return patternEvents; }
    juce::String getLoadedFileName() const { return loadedFileName; }
    int getCurrentStepDisplay() const { return lastStepIndex; }

    void togglePatternStep(int midiNote, int stepIndex);
    void setInternalSeqEnabled(bool enabled) { internalSeqEnabled.store(enabled); }
    bool isInternalSeqEnabled() const { return internalSeqEnabled.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void startVoiceForNote(int midiNote, float velocity);
    int classifySlice(int startSample, int endSample, double sr) const;

    juce::AudioFormatManager formatManager;
    juce::CriticalSection audioLock;

    juce::AudioBuffer<float> loopBuffer;
    double loopSampleRate = 44100.0;
    double hostSampleRate = 44100.0;
    juce::String loadedFileName { "Sin loop cargado (arrastra WAV/AIFF/MP3)" };

    std::vector<DrumSlice> slices;
    std::array<KitPad, 16> pads;
    std::vector<Loop2KitPreset::StepEvent> patternEvents;
    std::array<VoiceState, 16> voices;

    std::atomic<bool> internalSeqEnabled { true };
    int lastStepIndex = -1;
    double fallbackPpq = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Loop2KitProcessor)
};
