/*
    PluginList.h
    The VST3 plugins the Studio can host: a list of what was found (instruments for the four tracks, effects for their chains),
    saved between runs, and the scan that finds them. JUCE; message thread or a worker thread, never the audio thread.

    Each plugin file is scanned in its own process (KspScanner.exe): a plugin that crashes or hangs takes down the child, not the
    Studio, and the file is blacklisted (a "dead man's" file marks the plugin being scanned).
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <set>

namespace ksp::host
{

inline juce::File dataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KeyStepPro Studio");
}

inline juce::File knownListFile() { return dataFolder().getChildFile ("KnownPlugins.xml"); }
inline juce::File deadMansFile()  { return dataFolder().getChildFile ("PluginScan.txt"); }
inline juce::File hiddenFile()    { return dataFolder().getChildFile ("HiddenPlugins.xml"); }

/** KspScanner.exe: next to the Studio, or in the data folder. */
inline juce::File scannerExe()
{
    const juce::String name = "KspScanner.exe";
    const juce::File candidates[] = { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile (name),
                                      dataFolder().getChildFile (name) };
    for (auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return {};
}

/** Scans each plugin file in its own process. findPluginTypesFor returns false for a crash or a hang, so KnownPluginList
    blacklists the file and the scan goes on. */
class ProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    explicit ProcessScanner (juce::File exeToUse) : exe (std::move (exeToUse)) {}

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        const auto out = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("KspScan_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");

        juce::ChildProcess child;
        if (! child.start (juce::StringArray { exe.getFullPathName(), fileOrIdentifier, out.getFullPathName() }))
            return true;   // the scanner could not start (antivirus...): not the plugin's fault; it is simply not listed this time

        const auto deadline = juce::Time::getMillisecondCounter() + 60000u;
        while (child.isRunning())
        {
            if (shouldExit() || juce::Time::getMillisecondCounter() > deadline)
            {
                child.kill();
                out.deleteFile();
                return ! shouldExit();   // a hang is blacklisted like a crash; an abandoned scan is not
            }
            juce::Thread::sleep (20);
        }

        const bool ok = child.getExitCode() == 0 && out.existsAsFile();
        if (ok)
            if (auto xml = juce::XmlDocument::parse (out))
                for (auto* e : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*e))
                        result.add (d.release());
                }

        out.deleteFile();
        return ok;
    }

private:
    juce::File exe;
};

class PluginList
{
public:
    PluginList() { load(); }

    juce::KnownPluginList known;

    void load()
    {
        if (auto xml = juce::XmlDocument::parse (knownListFile()))
            known.recreateFromXml (*xml);
        hidden.clear();
        if (auto xml = juce::XmlDocument::parse (hiddenFile()))
            for (auto* e : xml->getChildIterator())
                hidden.insert (e->getStringAttribute ("id"));
    }

    /** Plugins the user hid from the lists (kept, not deleted). Keyed by JUCE's identifier string, so a re-scan keeps them hidden. */
    bool isHidden (const juce::PluginDescription& d) const { return hidden.count (d.createIdentifierString()) != 0; }

    void setHidden (const juce::PluginDescription& d, bool hide)
    {
        if (hide)
            hidden.insert (d.createIdentifierString());
        else
            hidden.erase (d.createIdentifierString());
        saveHidden();
    }

    std::vector<juce::PluginDescription> all() const
    {
        std::vector<juce::PluginDescription> out;
        for (const auto& d : known.getTypes())
            out.push_back (d);
        return out;
    }

    int numHidden() const { return static_cast<int> (hidden.size()); }

    void save() const
    {
        if (auto xml = known.createXml())
            if (dataFolder().createDirectory())
                xml->writeTo (knownListFile(), {});
    }

    /** Instruments (what a track plays) and effects (what its chain holds), without the hidden ones; the Studio itself is never offered. */
    std::vector<juce::PluginDescription> instruments() const { return pick (true); }
    std::vector<juce::PluginDescription> effects() const { return pick (false); }

    /** Blocking (a worker thread): scans the default VST3 folders, a file at a time in the scanner process. `progress` gets the
        file being scanned and returns false to stop. Returns the number of plugins the list holds afterwards; -1 without the
        scanner exe (nothing is scanned in this process: a plugin that crashes would take the Studio down). */
    int scan (const std::function<bool (const juce::String&)>& progress)
    {
        const auto exe = scannerExe();
        if (exe == juce::File())
            return -1;

        known.setCustomScanner (std::make_unique<ProcessScanner> (exe));
        juce::VST3PluginFormat format;
        juce::PluginDirectoryScanner scanner (known, format, format.getDefaultLocationsToSearch(), true, deadMansFile());
        juce::String name;
        while (scanner.scanNextFile (true, name))
            if (progress && ! progress (name))
                break;
        known.setCustomScanner (nullptr);
        save();
        return known.getNumTypes();
    }

private:
    std::set<juce::String> hidden;

    void saveHidden() const
    {
        juce::XmlElement root ("HIDDEN");
        for (const auto& id : hidden)
            root.createNewChildElement ("PLUGIN")->setAttribute ("id", id);
        if (dataFolder().createDirectory())
            root.writeTo (hiddenFile(), {});
    }

    std::vector<juce::PluginDescription> pick (bool instrument) const
    {
        std::vector<juce::PluginDescription> out;
        for (const auto& d : known.getTypes())
            if (d.isInstrument == instrument && d.manufacturerName != "KeyStepPro" && ! isHidden (d))
                out.push_back (d);
        return out;
    }
};

} // namespace ksp::host
