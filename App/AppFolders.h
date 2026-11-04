/*
    AppFolders.h
    Where the app keeps its own files: Documents/KeyStepPro Studio (the library, backups and the like).
*/

#pragma once

#include <juce_core/juce_core.h>

namespace ksp
{

inline juce::File dataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KeyStepPro Studio");
}

} // namespace ksp
