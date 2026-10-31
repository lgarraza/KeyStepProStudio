/*
    TrackInstrument.h
    One VST3 instrument hosted for a track of the Studio: MIDI in (the notes of the KeyStep Pro's track), stereo audio out, its
    description and state kept as text so the Studio project can save and restore it. JUCE.

    Threads: create(), the state calls and destruction belong to the message thread (or the one thread that owns the instance);
    process() is the only call of the audio thread and does not allocate (its MidiBuffer and AudioBuffer are sized by prepare()).
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ksp::host
{

class TrackInstrument
{
public:
    /** Loads the plugin described by `d` (blocking: some plugins take seconds). nullptr and `error` when it cannot be loaded. */
    static std::unique_ptr<TrackInstrument> create (const juce::PluginDescription& d, double sampleRate, int blockSize, juce::String& error)
    {
        juce::AudioPluginFormatManager formats;
        formats.addFormat (new juce::VST3PluginFormat());
        auto plugin = formats.createPluginInstance (d, sampleRate, blockSize, error);
        if (plugin == nullptr)
            return nullptr;
        return std::unique_ptr<TrackInstrument> (new TrackInstrument (std::move (plugin), d, sampleRate, blockSize));
    }

    ~TrackInstrument()
    {
        if (plugin != nullptr)
            plugin->releaseResources();
    }

    const juce::PluginDescription& description() const noexcept { return desc; }
    juce::AudioPluginInstance& instance() noexcept { return *plugin; }

    /** The plugin's description as XML text (what the Studio project stores to find the plugin again). */
    juce::String descriptionXml() const
    {
        if (auto xml = desc.createXml())
            return xml->toString (juce::XmlElement::TextFormat().singleLine());
        return {};
    }

    /** The plugin's state (its preset and settings) as base64 text. */
    juce::String stateBase64() const
    {
        juce::MemoryBlock block;
        plugin->getStateInformation (block);
        return juce::Base64::toBase64 (block.getData(), block.getSize());
    }

    /** Restores a state saved by stateBase64(); false when the text is not valid base64. Message thread, while the audio thread is
        not inside process() (the engine takes the instrument out of the audio path around this call). */
    bool setStateBase64 (const juce::String& text)
    {
        if (text.isEmpty())
            return true;   // nothing saved: keep the plugin's own default
        juce::MemoryOutputStream out;
        if (! juce::Base64::convertFromBase64 (out, text))
            return false;
        plugin->setStateInformation (out.getData(), static_cast<int> (out.getDataSize()));
        return true;
    }

    /** Audio thread: renders `numSamples` of the stereo output into `left` / `right` (overwritten), playing `midi`. */
    void process (const juce::MidiBuffer& midi, float* left, float* right, int numSamples) noexcept
    {
        numSamples = juce::jmin (numSamples, work.getNumSamples());
        work.clear();
        scratchMidi.clear();
        scratchMidi.addEvents (midi, 0, numSamples, 0);
        juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), numSamples);
        plugin->processBlock (view, scratchMidi);
        const float* l = work.getReadPointer (0);
        const float* r = work.getNumChannels() > 1 ? work.getReadPointer (1) : l;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = l[i];
            right[i] = r[i];
        }
    }

    /** Sets the sample rate and block size (the audio device changed): not while the audio thread may be inside process(). */
    void prepare (double sampleRate, int blockSize)
    {
        plugin->releaseResources();
        plugin->setPlayConfigDetails (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(), sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
        work.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), blockSize);
        scratchMidi.ensureSize (4096);
    }

    /** Audio thread: silences the voices (a note that was held when the instrument left the audio path). */
    void allNotesOff() noexcept { plugin->reset(); }

private:
    TrackInstrument (std::unique_ptr<juce::AudioPluginInstance> p, const juce::PluginDescription& d, double sampleRate, int blockSize)
        : plugin (std::move (p)), desc (d)
    {
        // an instrument: no audio in, a stereo main output; a plugin that insists on another layout keeps its own
        auto layout = plugin->getBusesLayout();
        for (auto& in : layout.inputBuses)
            in = juce::AudioChannelSet::disabled();
        if (! layout.outputBuses.isEmpty())
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        for (int i = 1; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
        if (! plugin->setBusesLayout (layout))
            plugin->enableAllBuses();

        prepare (sampleRate, blockSize);
    }

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription desc;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer scratchMidi;
};

} // namespace ksp::host
