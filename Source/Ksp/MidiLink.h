/*
    MidiLink.h
    The KeyStep Pro's USB MIDI port pair (JUCE): finds it by name, sends SysEx frames and waits for the reply.

    Requests are sent one at a time from a non-audio thread (the message thread or a worker): send() blocks until
    a frame matching the predicate arrives or the timeout runs out. Incoming SysEx is queued by the MIDI thread
    under a lock; nothing here runs on the audio thread.
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <deque>
#include <functional>
#include "Protocol.h"

namespace ksp
{

class MidiLink : private juce::MidiInputCallback
{
public:
    /** First input and output whose name contains `match` (case-insensitive), or nothing. */
    static std::unique_ptr<MidiLink> open (const juce::String& match, juce::String& error)
    {
        juce::MidiDeviceInfo inInfo, outInfo;
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            if (d.name.containsIgnoreCase (match)) { inInfo = d; break; }
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name.containsIgnoreCase (match)) { outInfo = d; break; }

        if (inInfo.identifier.isEmpty() || outInfo.identifier.isEmpty())
        {
            error = "No MIDI input and output named \"" + match + "\". Is the KeyStep Pro connected over USB, "
                    "and MIDI Control Center closed?";
            return nullptr;
        }

        std::unique_ptr<MidiLink> link (new MidiLink());
        link->output = juce::MidiOutput::openDevice (outInfo.identifier);
        link->input = juce::MidiInput::openDevice (inInfo.identifier, link.get());
        if (link->output == nullptr || link->input == nullptr)
        {
            error = "Could not open \"" + inInfo.name + "\" / \"" + outInfo.name + "\" (another app may be using it).";
            return nullptr;
        }
        link->inName = inInfo.name;
        link->outName = outInfo.name;
        link->input->start();
        return link;
    }

    ~MidiLink() override
    {
        if (input != nullptr)
            input->stop();
    }

    const juce::String& inputName() const noexcept  { return inName; }
    const juce::String& outputName() const noexcept { return outName; }

    /** Pause after a frame that gets no reply (open session, bulk SET). KSCON writes with 12 ms. */
    void setGapMs (int ms) noexcept { gapMs = ms; }

    using Match = std::function<bool (const Bytes&)>;

    /** Sends one frame and returns the first incoming frame for which `match` is true, or nothing after the
        timeout. Frames that do not match (ACKs, stray replies) are dropped. With no `match`, returns nothing
        after a short pause (for requests the device does not answer, like openSession). */
    std::optional<Bytes> send (const Bytes& frameToSend, Match match, int timeoutMs = 500)
    {
        {
            const juce::ScopedLock sl (lock);
            inbox.clear();
        }
        // JUCE wants the SysEx body without F0 / F7.
        output->sendMessageNow (juce::MidiMessage::createSysExMessage (frameToSend.data() + 1,
                                                                       static_cast<int> (frameToSend.size()) - 2));
        if (! match)
        {
            juce::Thread::sleep (gapMs);
            return std::nullopt;
        }

        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
        for (;;)
        {
            {
                const juce::ScopedLock sl (lock);
                while (! inbox.empty())
                {
                    auto f = std::move (inbox.front());
                    inbox.pop_front();
                    if (match (f))
                        return f;
                }
            }
            if (juce::Time::getMillisecondCounter() >= deadline)
                return std::nullopt;
            arrived.wait (5);
        }
    }

    /** Everything that is not SysEx (notes, clock, Start/Stop) goes to `fn` on the MIDI thread; an empty one stops it. */
    void setListener (std::function<void (const juce::MidiMessage&)> fn)
    {
        const juce::ScopedLock sl (lock);
        listener = std::move (fn);
    }

    /** send() with retries: for reads only (a dropped reply is just asked for again). */
    std::optional<Bytes> request (const Bytes& frameToSend, Match match, int tries = 3, int timeoutMs = 500)
    {
        for (int i = 0; i < tries; ++i)
            if (auto r = send (frameToSend, match, timeoutMs))
                return r;
        return std::nullopt;
    }

private:
    MidiLink() = default;

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (! m.isSysEx())
        {
            std::function<void (const juce::MidiMessage&)> fn;
            {
                const juce::ScopedLock sl (lock);
                fn = listener;
            }
            if (fn)
                fn (m);
            return;
        }
        const auto* raw = m.getRawData();
        Bytes f (raw, raw + m.getRawDataSize());   // includes F0 .. F7
        {
            const juce::ScopedLock sl (lock);
            inbox.push_back (std::move (f));
        }
        arrived.signal();
    }

    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String inName, outName;
    juce::CriticalSection lock;
    std::deque<Bytes> inbox;
    std::function<void (const juce::MidiMessage&)> listener;
    juce::WaitableEvent arrived;
    int gapMs = 20;
};

} // namespace ksp
