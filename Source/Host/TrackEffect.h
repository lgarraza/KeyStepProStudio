/*
    TrackEffect.h
    One hosted VST3 EFFECT: a stereo bus in, the same stereo bus out (processed in place), no MIDI. It sits where the Studio offers an effect
    position: the insert chain of a track or the master chain. Its description and state are kept as text for the Studio project. JUCE.

    Threads and design as TrackInstrument.h (create / state calls on the message thread, processStereo() on the audio thread without
    allocating).
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ksp::host
{

class TrackEffect
{
public:
    /** Loads the effect described by `d` (blocking). nullptr and `error` when it cannot be loaded. */
    static std::unique_ptr<TrackEffect> create (const juce::PluginDescription& d, double sampleRate, int blockSize, juce::String& error)
    {
        juce::AudioPluginFormatManager formats;
        formats.addFormat (new juce::VST3PluginFormat());
        auto plugin = formats.createPluginInstance (d, sampleRate, blockSize, error);
        if (plugin == nullptr)
            return nullptr;
        return std::unique_ptr<TrackEffect> (new TrackEffect (std::move (plugin), d, sampleRate, blockSize));
    }

    ~TrackEffect()
    {
        if (plugin != nullptr)
            plugin->releaseResources();
    }

    const juce::PluginDescription& description() const noexcept { return desc; }
    juce::AudioPluginInstance& instance() noexcept { return *plugin; }

    juce::String descriptionXml() const
    {
        if (auto xml = desc.createXml())
            return xml->toString (juce::XmlElement::TextFormat().singleLine());
        return {};
    }

    juce::String stateBase64() const
    {
        juce::MemoryBlock block;
        plugin->getStateInformation (block);
        return juce::Base64::toBase64 (block.getData(), block.getSize());
    }

    bool setStateBase64 (const juce::String& text)
    {
        if (text.isEmpty())
            return true;
        juce::MemoryOutputStream out;
        if (! juce::Base64::convertFromBase64 (out, text))
            return false;
        plugin->setStateInformation (out.getData(), static_cast<int> (out.getDataSize()));
        return true;
    }

    void prepare (double sampleRate, int blockSize)
    {
        plugin->releaseResources();
        plugin->setPlayConfigDetails (2, 2, sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
        work.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), blockSize);
        emptyMidi.clear();
    }

    /** Audio thread: processes `left` / `right` in place. */
    void processStereo (float* left, float* right, int numSamples) noexcept
    {
        const int cap = work.getNumSamples();
        for (int pos = 0; pos < numSamples; pos += cap)
        {
            const int n = juce::jmin (cap, numSamples - pos);
            work.clear();
            juce::FloatVectorOperations::copy (work.getWritePointer (0), left + pos, n);
            juce::FloatVectorOperations::copy (work.getWritePointer (1), right + pos, n);
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), n);
            plugin->processBlock (view, emptyMidi);
            juce::FloatVectorOperations::copy (left + pos, work.getReadPointer (0), n);
            juce::FloatVectorOperations::copy (right + pos, work.getReadPointer (work.getNumChannels() > 1 ? 1 : 0), n);
        }
    }

    /** Audio thread: clears the effect's tail (it left the audio path). */
    void flush() noexcept { plugin->reset(); }

private:
    TrackEffect (std::unique_ptr<juce::AudioPluginInstance> p, const juce::PluginDescription& d, double sampleRate, int blockSize)
        : plugin (std::move (p)), desc (d)
    {
        // a stereo effect: a stereo main input and output; side-chain and other buses off; a plugin that insists on another layout keeps its own
        auto layout = plugin->getBusesLayout();
        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        for (int i = 1; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
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
    juce::MidiBuffer emptyMidi;
};

} // namespace ksp::host
