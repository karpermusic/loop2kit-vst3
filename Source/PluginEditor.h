#pragma once

#include "PluginProcessor.h"

class Loop2KitEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             public juce::DragAndDropContainer,
                             private juce::Timer
{
public:
    explicit Loop2KitEditor(Loop2KitProcessor&);
    ~Loop2KitEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void openFileChooser();
    void dragMidiToDaw();

    Loop2KitProcessor& processor;

    juce::TextButton importBtn { "Importar Loop" };
    juce::TextButton analyzeBtn { "Re-segmentar" };
    juce::TextButton dragMidiBtn { "Arrastrar MIDI al DAW" };
    juce::ToggleButton seqToggle { "Secuenciador Sync" };

    juce::Slider sensSlider, gapSlider, swingSlider, pitchSlider;
    juce::Label sensLabel, gapLabel, swingLabel, pitchLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensAtt, gapAtt, swingAtt, pitchAtt;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::array<juce::TextButton, 16> padButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Loop2KitEditor)
};
