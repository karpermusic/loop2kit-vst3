#include "PluginEditor.h"

Loop2KitEditor::Loop2KitEditor(Loop2KitProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(860, 560);

    addAndMakeVisible(importBtn);
    importBtn.onClick = [this] { openFileChooser(); };

    addAndMakeVisible(analyzeBtn);
    analyzeBtn.onClick = [this] { processor.analyzeCurrentLoop(); repaint(); };

    addAndMakeVisible(dragMidiBtn);
    dragMidiBtn.onClick = [this] { dragMidiToDaw(); };

    addAndMakeVisible(seqToggle);
    seqToggle.setToggleState(processor.isInternalSeqEnabled(), juce::dontSendNotification);
    seqToggle.onClick = [this] { processor.setInternalSeqEnabled(seqToggle.getToggleState()); };

    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& text)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 18);
        addAndMakeVisible(s);
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };

    setupKnob(sensSlider, sensLabel, "Sensibilidad");
    setupKnob(gapSlider, gapLabel, "Min Gap (ms)");
    setupKnob(swingSlider, swingLabel, "Swing (%)");
    setupKnob(pitchSlider, pitchLabel, "Pitch (st)");

    sensAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "sensitivity", sensSlider);
    gapAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "mingap", gapSlider);
    swingAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "swing", swingSlider);
    pitchAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "pitch", pitchSlider);

    const auto& pads = processor.getPads();
    for (int i = 0; i < 16; ++i)
    {
        padButtons[i].setButtonText(pads[i].name + "\n(" + juce::String(pads[i].midiNote) + ")");
        padButtons[i].onClick = [this, i] { processor.triggerPadManual(i, 0.95f); };
        addAndMakeVisible(padButtons[i]);
    }

    startTimerHz(20);
}

Loop2KitEditor::~Loop2KitEditor()
{
    stopTimer();
}

void Loop2KitEditor::timerCallback()
{
    repaint();
}

bool Loop2KitEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aiff")
            || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".mp3")
            || f.endsWithIgnoreCase(".flac"))
            return true;
    return false;
}

void Loop2KitEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (!files.isEmpty())
    {
        processor.loadAudioFile(juce::File(files[0]));
        repaint();
    }
}

void Loop2KitEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Selecciona un Drum Loop", juce::File{}, "*.wav;*.aiff;*.aif;*.mp3;*.flac");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file.existsAsFile())
            {
                processor.loadAudioFile(file);
                repaint();
            }
        });
}

void Loop2KitEditor::dragMidiToDaw()
{
    const auto tempMidi = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("Loop2Kit_Pattern.mid");
    if (processor.exportPatternToMidiFile(tempMidi))
        performExternalDragDropOfFiles({ tempMidi.getFullPathName() }, false);
}

void Loop2KitEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111317));

    // Header
    g.setColour(juce::Colour(0xff181b20));
    g.fillRoundedRectangle(14.0f, 12.0f, static_cast<float>(getWidth() - 28), 46.0f, 6.0f);

    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("Loop2Kit VST3 / AU  ·  " + processor.getLoadedFileName(),
               26, 20, 460, 28, juce::Justification::centredLeft);

    // Waveform Box
    juce::Rectangle<float> waveArea(14.0f, 70.0f, static_cast<float>(getWidth() - 28), 120.0f);
    g.setColour(juce::Colour(0xff13161b));
    g.fillRoundedRectangle(waveArea, 6.0f);
    g.setColour(juce::Colour(0xff2a2f38));
    g.drawRoundedRectangle(waveArea, 6.0f, 1.0f);

    const auto& buf = processor.getLoopBuffer();
    if (buf.getNumSamples() > 0)
    {
        const float* data = buf.getReadPointer(0);
        const int total = buf.getNumSamples();
        const int w = static_cast<int>(waveArea.getWidth());
        const float midY = waveArea.getCentreY();

        g.setColour(juce::Colour(0xffe2e8f0).withAlpha(0.8f));
        for (int x = 0; x < w; ++x)
        {
            const int sIdx = juce::jlimit(0, total - 1, (x * total) / w);
            const float h = std::abs(data[sIdx]) * (waveArea.getHeight() * 0.44f);
            g.drawVerticalLine(static_cast<int>(waveArea.getX()) + x, midY - h, midY + h);
        }

        g.setColour(juce::Colour(0xfff59e0b));
        for (const auto& sl : processor.getSlices())
        {
            const float sx = waveArea.getX() + (static_cast<float>(sl.startSample) / total) * waveArea.getWidth();
            g.drawVerticalLine(static_cast<int>(sx), waveArea.getY() + 4.0f, waveArea.getBottom() - 4.0f);
        }
    }
    else
    {
        g.setColour(juce::Colour(0xff94a3b8));
        g.setFont(13.0f);
        g.drawText("Arrastra un archivo WAV / AIFF / MP3 aqui o pulsa 'Importar Loop'",
                   waveArea, juce::Justification::centred);
    }

    // Sequencer Step Bar Footer
    juce::Rectangle<float> seqArea(14.0f, 470.0f, static_cast<float>(getWidth() - 28), 72.0f);
    g.setColour(juce::Colour(0xff13161b));
    g.fillRoundedRectangle(seqArea, 6.0f);

    const int curStep = processor.getCurrentStepDisplay();
    const float stepW = seqArea.getWidth() / 32.0f;
    for (int s = 0; s < 32; ++s)
    {
        juce::Rectangle<float> cell(seqArea.getX() + s * stepW + 2.0f, seqArea.getY() + 18.0f, stepW - 4.0f, 42.0f);
        g.setColour(s == curStep ? juce::Colour(0xfff59e0b).withAlpha(0.35f) : juce::Colour(0xff1e2229));
        g.fillRoundedRectangle(cell, 3.0f);
    }
}

void Loop2KitEditor::resized()
{
    importBtn.setBounds(getWidth() - 375, 21, 108, 28);
    analyzeBtn.setBounds(getWidth() - 260, 21, 100, 28);
    dragMidiBtn.setBounds(getWidth() - 152, 21, 132, 28);

    // 4x4 Drum Kit Pads on left
    const int padStartX = 14;
    const int padStartY = 204;
    const int padW = 112;
    const int padH = 56;
    const int gap = 8;

    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            const int padIdx = (3 - r) * 4 + c;
            padButtons[padIdx].setBounds(padStartX + c * (padW + gap), padStartY + r * (padH + gap), padW, padH);
        }
    }

    // Knobs on right
    const int knobX = 505;
    sensLabel.setBounds(knobX, 206, 80, 18);
    sensSlider.setBounds(knobX, 226, 80, 84);

    gapLabel.setBounds(knobX + 90, 206, 80, 18);
    gapSlider.setBounds(knobX + 90, 226, 80, 84);

    swingLabel.setBounds(knobX + 180, 206, 80, 18);
    swingSlider.setBounds(knobX + 180, 226, 80, 84);

    pitchLabel.setBounds(knobX + 270, 206, 80, 18);
    pitchSlider.setBounds(knobX + 270, 226, 80, 84);

    seqToggle.setBounds(knobX, 336, 180, 28);
}
