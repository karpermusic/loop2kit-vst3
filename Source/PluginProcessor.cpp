#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

Loop2KitProcessor::Loop2KitProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();

    const char* defaultNames[16] = {
        "Kick", "Rimshot", "Snare", "Clap",
        "Snare Alt", "Low Tom", "Closed Hat", "Mid Tom",
        "Pedal Hat", "High Tom", "Open Hat", "Perc 1",
        "Perc 2", "Crash", "Perc 3", "Ride"
    };

    for (int i = 0; i < 16; ++i)
    {
        pads[i].midiNote = 36 + i;
        pads[i].name = defaultNames[i];
    }

    patternEvents = Loop2KitPreset::getInitialPattern();
}

juce::AudioProcessorValueTreeState::ParameterLayout Loop2KitProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("sensitivity", 1), "Sensibilidad",
        juce::NormalisableRange<float>(0.15f, 0.95f, 0.01f),
        Loop2KitPreset::defaultSensitivity));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mingap", 1), "Min Gap (ms)",
        juce::NormalisableRange<float>(35.0f, 180.0f, 1.0f),
        Loop2KitPreset::defaultMinGapMs));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("swing", 1), "Swing (%)",
        juce::NormalisableRange<float>(0.0f, 65.0f, 1.0f),
        Loop2KitPreset::defaultSwing));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("pitch", 1), "Master Pitch (st)",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 1.0f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("bars", 1), "Compases", 1, 4,
        Loop2KitPreset::defaultBars));

    return { params.begin(), params.end() };
}

void Loop2KitProcessor::prepareToPlay(double sampleRate, int)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    lastStepIndex = -1;
}

void Loop2KitProcessor::releaseResources()
{
}

bool Loop2KitProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto outSet = layouts.getMainOutputChannelSet();
    return outSet == juce::AudioChannelSet::mono()
        || outSet == juce::AudioChannelSet::stereo();
}

bool Loop2KitProcessor::loadAudioFile(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const int numSamples = static_cast<int>(reader->lengthInSamples);

    juce::AudioBuffer<float> tempBuffer(numChannels, numSamples);
    reader->read(&tempBuffer, 0, numSamples, 0, true, numChannels > 1);

    {
        const juce::ScopedLock sl(audioLock);
        loopBuffer = std::move(tempBuffer);
        loopSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        loadedFileName = file.getFileName();
    }

    analyzeCurrentLoop();
    return true;
}

int Loop2KitProcessor::classifySlice(int startSample, int endSample, double sr) const
{
    const int len = std::min(endSample - startSample, static_cast<int>(sr * 0.12));
    if (len < 16 || loopBuffer.getNumSamples() == 0)
        return 7;

    const float* data = loopBuffer.getReadPointer(0);
    const double dt = 1.0 / sr;
    const double alphaLow = dt / ((1.0 / (2.0 * juce::MathConstants<double>::pi * 180.0)) + dt);

    double lp = 0.0;
    double lowE = 0.0;
    double totalE = 1e-9;
    int zeroCrossings = 0;
    float prev = data[startSample];

    for (int i = 0; i < len; ++i)
    {
        const float x = data[startSample + i];
        lp = lp + alphaLow * (x - lp);
        lowE += lp * lp;
        totalE += static_cast<double>(x) * x;
        if (i > 0 && ((x >= 0.0f && prev < 0.0f) || (x < 0.0f && prev >= 0.0f)))
            ++zeroCrossings;
        prev = x;
    }

    const double lowRatio = lowE / totalE;
    const double zcr = static_cast<double>(zeroCrossings) / len;
    const double durSec = static_cast<double>(endSample - startSample) / sr;

    if (lowRatio > 0.45 && zcr < 0.085) return 0; // Kick (C1)
    if (zcr > 0.24) return durSec > 0.16 ? 6 : 5; // Open Hat (A#1) / Closed Hat (F#1)
    if (zcr > 0.16 && lowRatio < 0.15) return 3; // Clap (D#1)
    if (zcr >= 0.09) return 2; // Snare (D1)
    if (lowRatio > 0.28) return 4; // Tom (F1)
    return 7; // Perc
}

void Loop2KitProcessor::analyzeCurrentLoop()
{
    const juce::ScopedLock sl(audioLock);
    const int totalSamples = loopBuffer.getNumSamples();
    if (totalSamples < 512)
        return;

    const float sensitivity = apvts.getRawParameterValue("sensitivity")->load();
    const float minGapMs = apvts.getRawParameterValue("mingap")->load();
    const int bars = static_cast<int>(apvts.getRawParameterValue("bars")->load());

    const int win = 512;
    const int hop = 128;
    const int numFrames = std::max(1, (totalSamples - win) / hop);
    const float* mono = loopBuffer.getReadPointer(0);

    std::vector<float> energy(numFrames, 0.0f);
    float maxE = 1e-6f;

    for (int f = 0; f < numFrames; ++f)
    {
        const int start = f * hop;
        float sum = 0.0f;
        for (int i = 1; i < win; ++i)
        {
            const float diff = mono[start + i] - 0.7f * mono[start + i - 1];
            sum += diff * diff + 0.35f * mono[start + i] * mono[start + i];
        }
        energy[f] = std::sqrt(sum / win);
        if (energy[f] > maxE) maxE = energy[f];
    }

    for (auto& e : energy) e /= maxE;

    const int minGapFrames = std::max(2, static_cast<int>(std::round(((minGapMs / 1000.0) * loopSampleRate) / hop)));
    const float sensFactor = 1.15f - sensitivity;
    const float floorVal = 0.02f + (1.0f - sensitivity) * 0.08f;

    std::vector<int> onsetFrames;
    if (energy[0] > floorVal * 1.4f)
        onsetFrames.push_back(0);

    for (int f = 2; f < numFrames - 1; ++f)
    {
        const float nov = std::max(0.0f, energy[f] - energy[f - 1]);
        const float novPrev = std::max(0.0f, energy[f - 1] - energy[f - 2]);
        const float novNext = std::max(0.0f, energy[f + 1] - energy[f]);

        const int w0 = std::max(0, f - 12);
        const int w1 = std::min(numFrames - 1, f + 12);
        float lSum = 0.0f;
        for (int k = w0; k <= w1; ++k)
            lSum += std::max(0.0f, energy[k] - (k > 0 ? energy[k - 1] : 0.0f));

        const float thresh = (lSum / (w1 - w0 + 1)) * (1.25f + sensFactor * 2.1f) + floorVal * 0.45f;
        if (nov >= novPrev && nov > novNext && nov > thresh && energy[f] > floorVal)
        {
            if (onsetFrames.empty() || (f - onsetFrames.back()) >= minGapFrames)
                onsetFrames.push_back(f);
        }
    }

    if (onsetFrames.empty())
        onsetFrames.push_back(0);

    slices.clear();
    for (auto& p : pads) p.assignedSlice = -1;
    patternEvents.clear();

    const int totalSteps = bars * 16;
    const double loopDurSec = static_cast<double>(totalSamples) / loopSampleRate;
    const double stepDurSec = loopDurSec / totalSteps;

    const int categoryToPad[8] = { 0, 1, 2, 3, 5, 6, 10, 11 };

    for (size_t i = 0; i < onsetFrames.size(); ++i)
    {
        DrumSlice ds;
        ds.index = static_cast<int>(i);
        ds.startSample = onsetFrames[i] * hop;
        ds.endSample = (i + 1 < onsetFrames.size()) ? onsetFrames[i + 1] * hop : totalSamples;
        ds.category = classifySlice(ds.startSample, ds.endSample, loopSampleRate);

        float pk = 0.0f;
        for (int s = ds.startSample; s < ds.endSample; ++s)
            pk = std::max(pk, std::abs(mono[s]));

        ds.peak = pk;
        ds.velocity = juce::jlimit(24, 127, static_cast<int>(std::round(std::pow(std::min(1.0f, pk), 0.72f) * 127.0f)));
        slices.push_back(ds);

        const int padIdx = categoryToPad[juce::jlimit(0, 7, ds.category)];
        if (pads[padIdx].assignedSlice < 0 || ds.peak > slices[pads[padIdx].assignedSlice].peak)
            pads[padIdx].assignedSlice = ds.index;

        const double startSec = static_cast<double>(ds.startSample) / loopSampleRate;
        const int stepIdx = juce::jlimit(0, totalSteps - 1, static_cast<int>(std::round(startSec / stepDurSec)));
        patternEvents.push_back({ pads[padIdx].midiNote, stepIdx, ds.velocity });
    }
}

void Loop2KitProcessor::startVoiceForNote(int midiNote, float velocity)
{
    const int padIdx = midiNote - 36;
    if (padIdx < 0 || padIdx >= 16) return;

    const auto& pad = pads[padIdx];
    if (pad.muted || pad.assignedSlice < 0 || pad.assignedSlice >= static_cast<int>(slices.size()))
        return;

    const auto& sl = slices[pad.assignedSlice];
    const float masterPitch = apvts.getRawParameterValue("pitch")->load();
    const double rate = (loopSampleRate / hostSampleRate) * std::pow(2.0, (pad.pitchSemitones + masterPitch) / 12.0);

    VoiceState& v = voices[padIdx];
    v.active = true;
    v.samplePos = static_cast<double>(sl.startSample);
    v.endSample = sl.endSample;
    v.rate = rate;
    v.gain = pad.gain * velocity;
    v.env = 1.0f;
    v.envDecay = 0.9999f;
}

void Loop2KitProcessor::triggerPadManual(int padIndex, float velocity)
{
    const juce::ScopedLock sl(audioLock);
    if (padIndex >= 0 && padIndex < 16)
        startVoiceForNote(36 + padIndex, velocity);
}

void Loop2KitProcessor::togglePatternStep(int midiNote, int stepIndex)
{
    const juce::ScopedLock sl(audioLock);
    for (auto it = patternEvents.begin(); it != patternEvents.end(); ++it)
    {
        if (it->midiNote == midiNote && it->stepIndex == stepIndex)
        {
            patternEvents.erase(it);
            return;
        }
    }
    patternEvents.push_back({ midiNote, stepIndex, 110 });
}

void Loop2KitProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const juce::ScopedLock sl(audioLock);

    // 1. Incoming MIDI triggers
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            startVoiceForNote(msg.getNoteNumber(), msg.getFloatVelocity());
    }

    // 2. Host-Synced Pattern Sequencer -> outputs MIDI & triggers internal Kit
    if (internalSeqEnabled.load() && !patternEvents.empty())
    {
        double bpm = Loop2KitPreset::defaultBpm;
        bool isPlaying = true;
        double ppqPos = fallbackPpq;

        if (auto* playHead = getPlayHead())
        {
            if (const auto pos = playHead->getPosition())
            {
                if (pos->getBpm().hasValue())
                    bpm = *pos->getBpm();
                isPlaying = pos->getIsPlaying();
                if (pos->getPpqPosition().hasValue())
                    ppqPos = *pos->getPpqPosition();
            }
        }

        if (isPlaying)
        {
            const int bars = static_cast<int>(apvts.getRawParameterValue("bars")->load());
            const int totalSteps = bars * 16;
            const double stepsPerBeat = 4.0;
            const int currentStep = static_cast<int>(std::floor(ppqPos * stepsPerBeat)) % totalSteps;

            if (currentStep != lastStepIndex)
            {
                lastStepIndex = currentStep;
                for (const auto& ev : patternEvents)
                {
                    if (ev.stepIndex == currentStep)
                    {
                        const float vel = static_cast<float>(ev.velocity) / 127.0f;
                        startVoiceForNote(ev.midiNote, vel);
                        midiMessages.addEvent(juce::MidiMessage::noteOn(10, ev.midiNote, static_cast<juce::uint8>(ev.velocity)), 0);
                    }
                }
            }

            const double beatsPerSample = (bpm / 60.0) / hostSampleRate;
            fallbackPpq += beatsPerSample * buffer.getNumSamples();
        }
    }

    // 3. Render active sampler voices
    const int numSamples = buffer.getNumSamples();
    const int numOutChannels = buffer.getNumChannels();
    const int loopChannels = loopBuffer.getNumChannels();

    if (loopChannels == 0 || loopBuffer.getNumSamples() == 0)
        return;

    for (auto& v : voices)
    {
        if (!v.active) continue;

        for (int s = 0; s < numSamples; ++s)
        {
            const int idx0 = static_cast<int>(v.samplePos);
            if (idx0 >= v.endSample - 1)
            {
                v.active = false;
                break;
            }

            const float frac = static_cast<float>(v.samplePos - idx0);
            for (int ch = 0; ch < numOutChannels; ++ch)
            {
                const int srcCh = std::min(ch, loopChannels - 1);
                const float* src = loopBuffer.getReadPointer(srcCh);
                const float sample = src[idx0] * (1.0f - frac) + src[idx0 + 1] * frac;
                buffer.addSample(ch, s, sample * v.gain * v.env);
            }

            v.samplePos += v.rate;
            v.env *= v.envDecay;
        }
    }
}

bool Loop2KitProcessor::exportPatternToMidiFile(const juce::File& targetFile) const
{
    const juce::ScopedLock sl(audioLock);
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(480);

    juce::MidiMessageSequence seq;
    const float swingPct = apvts.getRawParameterValue("swing")->load();
    const int ticksPerStep = 120; // 1/16 note at 480 PPQ

    for (const auto& ev : patternEvents)
    {
        const int swingTicks = (ev.stepIndex % 2 == 1)
            ? static_cast<int>(ticksPerStep * (swingPct / 100.0f) * 0.35f)
            : 0;
        const double startTick = static_cast<double>(ev.stepIndex * ticksPerStep + swingTicks);
        seq.addEvent(juce::MidiMessage::noteOn(10, ev.midiNote, static_cast<juce::uint8>(ev.velocity)), startTick);
        seq.addEvent(juce::MidiMessage::noteOff(10, ev.midiNote), startTick + 90.0);
    }

    seq.updateMatchedPairs();
    midiFile.addTrack(seq);

    if (targetFile.existsAsFile())
        targetFile.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(targetFile.createOutputStream());
    if (stream == nullptr)
        return false;

    return midiFile.writeTo(*stream);
}

void Loop2KitProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void Loop2KitProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorEditor* Loop2KitProcessor::createEditor()
{
    return new Loop2KitEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Loop2KitProcessor();
}
