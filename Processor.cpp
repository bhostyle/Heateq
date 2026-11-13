#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

class HeatEQ final : public juce::AudioProcessor
{
    using State = juce::AudioProcessorValueTreeState;
    using Coefficients = juce::dsp::IIR::ArrayCoefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;
    static constexpr std::array<float, 6> centres { 80, 200, 500, 1500, 4000, 10000 };

    static juce::String bandId(int index, const char* suffix)
    {
        return "band" + juce::String(index + 1) + suffix;
    }

    static State::ParameterLayout makeParameters()
    {
        using namespace juce;
        State::ParameterLayout layout;
        auto add = [&layout](String id, String name, NormalisableRange<float> range, float initial)
        {
            layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, range, initial));
        };
        const NormalisableRange<float> frequency {20, 20000, 0.1f, 0.25f};
        const NormalisableRange<float> gain {-18, 18, 0.1f};
        for (int band = 0; band < 6; ++band)
        {
            const auto label = "Band " + String(band + 1) + " ";
            add(bandId(band, "Hz"), label + "Frequency (Hz)", frequency, centres[band]);
            add(bandId(band, "Gain"), label + "Gain (dB)", gain, 0);
            add(bandId(band, "Q"), label + "Q", {0.25f, 12, 0.01f, 0.35f}, 0.707f);
        }
        layout.add(std::make_unique<AudioParameterBool>(ParameterID{"hpOn", 1}, "High-pass enabled", false));
        layout.add(std::make_unique<AudioParameterBool>(ParameterID{"lpOn", 1}, "Low-pass enabled", false));
        add("hpHz", "High-pass (Hz)", frequency, 30);
        add("lpHz", "Low-pass (Hz)", frequency, 18000);
        add("output", "Output (dB)", gain, 0);
        return layout;
    }

public:
    HeatEQ()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          state(*this, nullptr, "HeatEQ", makeParameters())
    {
        for (int band = 0; band < 6; ++band)
            bands[band] = {raw(bandId(band, "Hz")), raw(bandId(band, "Gain")), raw(bandId(band, "Q"))};
    }

    const juce::String getName() const override { return "Heat EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }

    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    {
        const auto outputSet = layout.getMainOutputChannelSet();
        return outputSet == layout.getMainInputChannelSet()
            && (outputSet == juce::AudioChannelSet::mono() || outputSet == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay(double rate, int) override
    {
        sampleRate = rate;
        for (auto& channel : filters)
            for (auto& filter : channel)
            {
                *filter.coefficients = Coefficients::makePeakFilter(rate, 1000, 0.707f, 1.0f);
                filter.reset();
            }
        trim.reset(rate, 0.02);
        trim.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(output->load()));
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const auto frequency = [this](const std::atomic<float>* value)
        {
            return juce::jmin(value->load(), static_cast<float>(sampleRate * 0.49));
        };
        const auto channels = buffer.getNumChannels();
        const auto update = [&](int stage, const std::array<float, 6>& coefficients)
        {
            for (int channel = 0; channel < channels; ++channel)
                *filters[channel][stage].coefficients = coefficients;
        };

        const bool highPass = hpOn->load() > 0.5f;
        const bool lowPass = lpOn->load() > 0.5f;
        update(0, Coefficients::makeHighPass(sampleRate, frequency(hpHz)));
        for (int band = 0; band < 6; ++band)
        {
            const auto& controls = bands[band];
            const auto hz = frequency(controls.hz);
            const auto q = controls.q->load();
            const auto gain = juce::Decibels::decibelsToGain(controls.gain->load());
            auto coefficients = Coefficients::makePeakFilter(sampleRate, hz, q, gain);
            if (band == 0)
                coefficients = Coefficients::makeLowShelf(sampleRate, hz, q, gain);
            if (band == 5)
                coefficients = Coefficients::makeHighShelf(sampleRate, hz, q, gain);
            update(band + 1, coefficients);
        }
        update(7, Coefficients::makeLowPass(sampleRate, frequency(lpHz)));

        if (highPass != previousHighPass || lowPass != previousLowPass)
            for (auto& channel : filters)
            {
                if (highPass != previousHighPass)
                    channel[0].reset();
                if (lowPass != previousLowPass)
                    channel[7].reset();
            }
        previousHighPass = highPass;
        previousLowPass = lowPass;

        trim.setTargetValue(juce::Decibels::decibelsToGain(output->load()));
        auto* const* samples = buffer.getArrayOfWritePointers();
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto gain = trim.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                auto value = samples[channel][sample];
                for (int stage = highPass ? 0 : 1; stage < (lowPass ? 8 : 7); ++stage)
                    value = filters[channel][stage].processSample(value);
                samples[channel][sample] = value * gain;
            }
        }
    }

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary(*xml, dest);
    }

    void setStateInformation(const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(state.state.getType().toString()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    std::atomic<float>* raw(const juce::String& id) { return state.getRawParameterValue(id); }
    struct Band { std::atomic<float>* hz; std::atomic<float>* gain; std::atomic<float>* q; };
    State state;
    std::array<Band, 6> bands {};
    std::atomic<float>* hpOn = raw("hpOn");
    std::atomic<float>* lpOn = raw("lpOn");
    std::atomic<float>* hpHz = raw("hpHz");
    std::atomic<float>* lpHz = raw("lpHz");
    std::atomic<float>* output = raw("output");
    std::array<std::array<Filter, 8>, 2> filters;
    juce::SmoothedValue<float> trim;
    double sampleRate = 44100;
    bool previousHighPass = false;
    bool previousLowPass = false;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HeatEQ(); }
