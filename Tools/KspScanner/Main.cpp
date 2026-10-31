// Out-of-process VST3 scanner: KspScanner.exe <plugin file> <result.xml>
// Loads one plugin file, writes the descriptions it finds to the XML and exits 0. If the plugin crashes, the process dies and the
// host (Source/Host/PluginList.h, ProcessScanner) blacklists that file and keeps scanning.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

int main (int argc, char** argv)
{
    if (argc < 3)
        return 2;

    juce::ScopedJuceInitialiser_GUI init;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::String::fromUTF8 (argv[1]));

    juce::XmlElement root ("SCAN");
    for (auto* d : found)
        root.addChildElement (d->createXml().release());

    return root.writeTo (juce::File (juce::String::fromUTF8 (argv[2])), {}) ? 0 : 3;
}
