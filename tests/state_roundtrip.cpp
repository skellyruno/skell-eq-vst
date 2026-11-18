#include "PluginProcessor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << "\n";
            std::exit(1);
        }
    }

    void setActual(SkellyEQAudioProcessor& processor, const juce::String& parameterID,
                   float actualValue)
    {
        auto* parameter = processor.state.getParameter(parameterID);
        require(parameter != nullptr, "parameter exists");
        const auto normalized = parameter->convertTo0to1(actualValue);
        require(std::isfinite(normalized), "parameter value normalizes to a finite value");
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalized));
    }

    float currentValue(const SkellyEQAudioProcessor& processor, const juce::String& parameterID)
    {
        const auto* value = processor.state.getRawParameterValue(parameterID);
        require(value != nullptr, "raw parameter exists");
        return value->load(std::memory_order_relaxed);
    }

    void expectNear(float actual, float expected, float tolerance, const char* message)
    {
        require(std::isfinite(actual), message);
        require(std::abs(actual - expected) <= tolerance, message);
    }

    int countChildren(const juce::ValueTree& tree, const juce::Identifier& type)
    {
        int count = 0;
        for (int i = 0; i < tree.getNumChildren(); ++i)
            if (tree.getChild(i).getType() == type) ++count;
        return count;
    }

    juce::ValueTree findSnapshot(const juce::ValueTree& root, const char* name)
    {
        return root.getChildWithName(juce::Identifier(name));
    }

    float snapshotValue(const juce::ValueTree& snapshot, const char* parameterID)
    {
        for (int i = 0; i < snapshot.getNumChildren(); ++i)
        {
            const auto node = snapshot.getChild(i);
            if (node.getType() == juce::Identifier("PARAM")
                && node.getProperty("id").toString() == parameterID
                && node.hasProperty("value"))
                return (float) node.getProperty("value");
        }
        require(false, "snapshot contains requested parameter");
        return 0.0f;
    }

    int parameterChildCount(const juce::ValueTree& root)
    {
        return countChildren(root, juce::Identifier("PARAM"));
    }

    void verifySnapshotContainsOnlyFlatParameters(const juce::ValueTree& snapshot,
                                                  int expectedParameterCount)
    {
        require(snapshot.isValid(), "snapshot exists");
        require(snapshot.getNumChildren() == expectedParameterCount,
                "snapshot contains every parameter exactly once");
        for (int i = 0; i < snapshot.getNumChildren(); ++i)
        {
            const auto node = snapshot.getChild(i);
            require(node.getType() == juce::Identifier("PARAM"),
                    "snapshot contains only PARAM nodes");
            require(node.getNumChildren() == 0, "snapshot does not contain nested nodes");
            require(node.hasProperty("id") && node.hasProperty("value"),
                    "snapshot parameter has id and value");
            require(std::isfinite((float) node.getProperty("value")),
                    "snapshot parameter value is finite");
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    SkellyEQAudioProcessor source;

    // Snapshot A: a deliberately distinct EQ curve and global routing state.
    setActual(source, "b1_freq", 1234.0f);
    setActual(source, "b1_gain", 6.5f);
    setActual(source, "b1_q", 2.25f);
    setActual(source, "b1_enabled", 0.0f);
    setActual(source, "phaseMode", 0.0f);
    setActual(source, "stereoMode", 1.0f);
    setActual(source, "outputGain", -3.0f);
    setActual(source, "analyzeMode", 2.0f);
    source.storeSnapshot(0);

    // Snapshot B: prove the second slot is independent of the first.
    setActual(source, "b1_freq", 9999.0f);
    setActual(source, "b1_gain", -7.25f);
    setActual(source, "b1_q", 0.65f);
    setActual(source, "b1_enabled", 1.0f);
    setActual(source, "phaseMode", 2.0f);
    setActual(source, "stereoMode", 0.0f);
    setActual(source, "outputGain", 2.0f);
    setActual(source, "analyzeMode", 3.0f);
    source.storeSnapshot(1);
    source.state.state.setProperty("selectedBand", 5, nullptr);

    juce::MemoryBlock savedState;
    source.getStateInformation(savedState);
    require(savedState.getSize() > 0, "host state serializes to a non-empty block");

    // Start with stale snapshots to verify that restore replaces, rather than
    // appends to, a previous project's A/B state.
    SkellyEQAudioProcessor restored;
    setActual(restored, "b1_freq", 100.0f);
    restored.storeSnapshot(0);
    restored.storeSnapshot(1);
    restored.setStateInformation(savedState.getData(), (int) savedState.getSize());

    expectNear(currentValue(restored, "b1_freq"), 9999.0f, 0.1f,
               "current state restores the live B curve");
    expectNear(currentValue(restored, "b1_gain"), -7.25f, 0.02f,
               "current state restores the live B gain");
    expectNear(currentValue(restored, "phaseMode"), 2.0f, 0.01f,
               "current state restores the live phase mode");
    expectNear(currentValue(restored, "stereoMode"), 0.0f, 0.01f,
               "current state restores the live stereo mode");

    auto restoredTree = restored.state.copyState();
    const auto snapshotA = findSnapshot(restoredTree, "snapshotA");
    const auto snapshotB = findSnapshot(restoredTree, "snapshotB");
    const int expectedParameters = parameterChildCount(restoredTree);
    verifySnapshotContainsOnlyFlatParameters(snapshotA, expectedParameters);
    verifySnapshotContainsOnlyFlatParameters(snapshotB, expectedParameters);
    expectNear(snapshotValue(snapshotA, "b1_freq"), 1234.0f, 0.1f,
               "snapshot A retains its frequency");
    expectNear(snapshotValue(snapshotA, "b1_gain"), 6.5f, 0.02f,
               "snapshot A retains its gain");
    expectNear(snapshotValue(snapshotB, "b1_freq"), 9999.0f, 0.1f,
               "snapshot B retains its frequency");
    expectNear(snapshotValue(snapshotB, "b1_gain"), -7.25f, 0.02f,
               "snapshot B retains its gain");
    require((int) restoredTree.getProperty("selectedBand", -1) == 5,
            "selected band survives host state restore");

    // Exercise the actual editor-facing load path, including host notifications.
    restored.loadSnapshot(0);
    expectNear(currentValue(restored, "b1_freq"), 1234.0f, 0.1f,
               "loading A restores A frequency");
    expectNear(currentValue(restored, "b1_gain"), 6.5f, 0.02f,
               "loading A restores A gain");
    expectNear(currentValue(restored, "phaseMode"), 0.0f, 0.01f,
               "loading A restores A phase mode");
    restored.loadSnapshot(1);
    expectNear(currentValue(restored, "b1_freq"), 9999.0f, 0.1f,
               "loading B restores B frequency");
    expectNear(currentValue(restored, "b1_gain"), -7.25f, 0.02f,
               "loading B restores B gain");
    expectNear(currentValue(restored, "phaseMode"), 2.0f, 0.01f,
               "loading B restores B phase mode");

    // Repeated saves must replace each slot and never nest previous snapshots.
    for (int i = 0; i < 12; ++i)
    {
        restored.storeSnapshot(0);
        restored.storeSnapshot(1);
    }
    restoredTree = restored.state.copyState();
    require(countChildren(restoredTree, juce::Identifier("snapshotA")) == 1,
            "repeated saves keep exactly one A snapshot");
    require(countChildren(restoredTree, juce::Identifier("snapshotB")) == 1,
            "repeated saves keep exactly one B snapshot");
    verifySnapshotContainsOnlyFlatParameters(findSnapshot(restoredTree, "snapshotA"),
                                             expectedParameters);
    verifySnapshotContainsOnlyFlatParameters(findSnapshot(restoredTree, "snapshotB"),
                                             expectedParameters);

    // Older host states without A/B nodes must not inherit snapshots from the
    // previously loaded project.
    SkellyEQAudioProcessor legacyState;
    setActual(legacyState, "b1_gain", 3.0f);
    juce::MemoryBlock legacyBlock;
    legacyState.getStateInformation(legacyBlock);
    restored.setStateInformation(legacyBlock.getData(), (int) legacyBlock.getSize());
    restoredTree = restored.state.copyState();
    require(!findSnapshot(restoredTree, "snapshotA").isValid(),
            "legacy state without A removes stale snapshot A");
    require(!findSnapshot(restoredTree, "snapshotB").isValid(),
            "legacy state without B removes stale snapshot B");

    // Malformed snapshot nodes are filtered by the production restore path.
    auto malformedTree = legacyState.state.copyState();
    malformedTree.setProperty("selectedBand", 99, nullptr);
    juce::ValueTree malformedA {juce::Identifier("snapshotA")};

    juce::ValueTree outOfRange {juce::Identifier("PARAM")};
    outOfRange.setProperty("id", "b1_gain", nullptr);
    outOfRange.setProperty("value", 999.0f, nullptr);
    malformedA.addChild(outOfRange, -1, nullptr);

    juce::ValueTree unknown {juce::Identifier("PARAM")};
    unknown.setProperty("id", "not_a_real_parameter", nullptr);
    unknown.setProperty("value", 4.0f, nullptr);
    malformedA.addChild(unknown, -1, nullptr);

    juce::ValueTree wrongType {juce::Identifier("NOT_PARAM")};
    wrongType.setProperty("id", "b1_freq", nullptr);
    wrongType.setProperty("value", 500.0f, nullptr);
    malformedA.addChild(wrongType, -1, nullptr);

    juce::ValueTree missingValue {juce::Identifier("PARAM")};
    missingValue.setProperty("id", "b1_q", nullptr);
    malformedA.addChild(missingValue, -1, nullptr);

    juce::ValueTree nonFinite {juce::Identifier("PARAM")};
    nonFinite.setProperty("id", "b1_freq", nullptr);
    nonFinite.setProperty("value", std::numeric_limits<double>::quiet_NaN(), nullptr);
    malformedA.addChild(nonFinite, -1, nullptr);

    // Valid decimal strings survive XML restoration; empty strings and numeric
    // prefixes with trailing text must be rejected rather than parsed as 0/1500.
    juce::ValueTree numericString {juce::Identifier("PARAM")};
    numericString.setProperty("id", "b1_freq", nullptr);
    numericString.setProperty("value", "1500.5", nullptr);
    malformedA.addChild(numericString, -1, nullptr);

    juce::ValueTree emptySnapshotValue {juce::Identifier("PARAM")};
    emptySnapshotValue.setProperty("id", "b1_q", nullptr);
    emptySnapshotValue.setProperty("value", "", nullptr);
    malformedA.addChild(emptySnapshotValue, -1, nullptr);

    juce::ValueTree trailingSnapshotValue {juce::Identifier("PARAM")};
    trailingSnapshotValue.setProperty("id", "b1_freq", nullptr);
    trailingSnapshotValue.setProperty("value", "1500Hz", nullptr);
    malformedA.addChild(trailingSnapshotValue, -1, nullptr);

    malformedTree.addChild(malformedA, -1, nullptr);

    // Root PARAM duplicates with malformed values must not overwrite the
    // previously valid legacy state with JUCE's permissive zero conversion.
    juce::ValueTree malformedRootGain {juce::Identifier("PARAM")};
    malformedRootGain.setProperty("id", "b1_gain", nullptr);
    malformedRootGain.setProperty("value", "not-a-number", nullptr);
    malformedTree.addChild(malformedRootGain, -1, nullptr);

    juce::ValueTree malformedRootQ {juce::Identifier("PARAM")};
    malformedRootQ.setProperty("id", "b1_q", nullptr);
    malformedRootQ.setProperty("value", "", nullptr);
    malformedTree.addChild(malformedRootQ, -1, nullptr);
    auto malformedXml = malformedTree.createXml();
    require(malformedXml != nullptr, "malformed test state creates XML");
    juce::MemoryBlock malformedBlock;
    juce::AudioProcessor::copyXmlToBinary(*malformedXml, malformedBlock);
    restored.setStateInformation(malformedBlock.getData(), (int) malformedBlock.getSize());

    restoredTree = restored.state.copyState();
    require((int) restoredTree.getProperty("selectedBand", -1) == 7,
            "selected band is clamped to the valid range");
    const auto sanitizedA = findSnapshot(restoredTree, "snapshotA");
    require(sanitizedA.isValid(), "sanitized snapshot A is retained");
    require(!findSnapshot(restoredTree, "snapshotB").isValid(),
            "missing snapshot B is not synthesized");
    expectNear(currentValue(restored, "b1_gain"), 3.0f, 0.02f,
               "malformed root numeric string cannot overwrite valid legacy gain");
    expectNear(currentValue(restored, "b1_q"), 1.0f, 0.02f,
               "empty root numeric string cannot reset valid legacy Q");
    expectNear(snapshotValue(sanitizedA, "b1_freq"), 1500.5f, 0.1f,
               "valid decimal-string snapshot value survives restore");
    bool sawClampedGain = false;
    for (int i = 0; i < sanitizedA.getNumChildren(); ++i)
    {
        const auto node = sanitizedA.getChild(i);
        require(node.getType() == juce::Identifier("PARAM"),
                "sanitized snapshot contains only PARAM nodes");
        const auto id = node.getProperty("id").toString();
        require(id == "b1_gain" || id == "b1_freq",
                "unknown and malformed parameter IDs are discarded");
        const float value = (float) node.getProperty("value");
        require(std::isfinite(value), "restored snapshot contains no NaN or infinity");
        if (id == "b1_gain")
        {
            expectNear(value, 18.0f, 0.02f, "out-of-range gain is clamped to its maximum");
            sawClampedGain = true;
        }
        else
        {
            require(value >= 20.0f && value <= 20000.0f,
                    "restored frequency stays within its parameter range");
        }
    }
    require(sawClampedGain, "out-of-range gain parameter is retained safely");

    // The editor-facing snapshot load path must apply the same strict parser.
    juce::ValueTree malformedLoad {juce::Identifier("PARAM")};
    malformedLoad.setProperty("id", "b1_q", nullptr);
    malformedLoad.setProperty("value", "garbage", nullptr);
    restored.state.state.getChildWithName(juce::Identifier("snapshotA"))
        .addChild(malformedLoad, -1, nullptr);
    restored.loadSnapshot(0);
    expectNear(currentValue(restored, "b1_q"), 1.0f, 0.02f,
               "malformed snapshot value is ignored when loading A");

    std::cout << "Skelly EQ A/B host-state roundtrip and malformed-state checks PASS\n";
    return 0;
}
