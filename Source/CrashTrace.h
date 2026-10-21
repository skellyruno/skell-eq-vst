#pragma once
#include <JuceHeader.h>

// Diagnostic only: called from lifecycle, state and editor routines, never processBlock.
// Avoid logging host project content or parameter values.
namespace SkellyTrace
{
inline void checkpoint(const char* message)
{
#if SKELLY_TRACE_LIFECYCLE
    static juce::CriticalSection traceLock;
    const juce::ScopedLock guard(traceLock);
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("SkellyAudio");
    if (! folder.isDirectory())
        folder.createDirectory();
    if (! folder.isDirectory())
        return;

    auto logFile = folder.getChildFile("SkellyEQ-v1.5-trace.txt");
    if (logFile.existsAsFile() && logFile.getSize() > 1024 * 1024)
        logFile.deleteFile();

    const auto timestamp = juce::Time::getCurrentTime().toISO8601(true);
    logFile.appendText(timestamp + " | " + juce::String(message) + "\n");
#else
    juce::ignoreUnused(message);
#endif
}
}
