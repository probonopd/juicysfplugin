/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiConstants.h"
#include "Util.h"
#include "GuiConstants.h"

using namespace std;
using Parameter = AudioProcessorValueTreeState::Parameter;

AudioProcessor* JUCE_CALLTYPE createPluginFilter();


//==============================================================================
JuicySFAudioProcessor::JuicySFAudioProcessor()
: AudioProcessor{getBusesProperties()}
, valueTreeState{
    *this,
    nullptr,
    "MYPLUGINSETTINGS",
    createParameterLayout()}
, fluidSynthModel{valueTreeState}
{
    MemoryBlock bookmarkBuffer;
    valueTreeState.state.appendChild({ "uiState", {
            { "width", GuiConstants::minWidth },
            { "height", GuiConstants::minHeight }
        }, {} }, nullptr);
    valueTreeState.state.appendChild({ "soundFont", {
        { "path", "" },
        { "bookmark", std::move(bookmarkBuffer) },
    }, {} }, nullptr);
    // no properties, no subtrees (yet)
    valueTreeState.state.appendChild({ "banks", {}, {} }, nullptr);
    // Which wave-seq step (0-3) the preset browser is currently targeting
    valueTreeState.state.setProperty("activeBrowserStep", 0, nullptr);
    
    initialiseSynth();
}

AudioProcessorValueTreeState::ParameterLayout JuicySFAudioProcessor::createParameterLayout() {
    // https://stackoverflow.com/a/8469002/5257399
    vector<unique_ptr<RangedAudioParameter>> params;
    // SoundFont 2.4 spec section 7.2: zero through 127, or 128.
    params.push_back(make_unique<AudioParameterInt>("bank", "which bank is selected in the soundfont", MidiConstants::midiMinValue, 128, MidiConstants::midiMinValue, "Bank"));
    // note: banks may be sparse, and lack a 0th preset. so defend against this.
    params.push_back(make_unique<AudioParameterInt>("preset", "which patch (aka patch, program, instrument) is selected in the soundfont", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Preset"));
    params.push_back(make_unique<AudioParameterInt>("attack", "volume envelope attack time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "A"));
    params.push_back(make_unique<AudioParameterInt>("decay", "volume envelope sustain attentuation", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "D"));
    params.push_back(make_unique<AudioParameterInt>("sustain", "volume envelope decay time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "S"));
    params.push_back(make_unique<AudioParameterInt>("release", "volume envelope release time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "R"));
    params.push_back(make_unique<AudioParameterInt>("filterCutOff", "low-pass filter cut-off frequency", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Cut"));
    params.push_back(make_unique<AudioParameterInt>("filterResonance", "low-pass filter resonance attentuation", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Res"));
    // Vector synthesis LFO / wave-sequence controls
    params.push_back(make_unique<AudioParameterFloat>("vectorLfoRate", "vector LFO / wave-seq rate (steps/sec)", NormalisableRange<float>(0.01f, 10.0f, 0.01f, 0.3f), 0.2f, "Rate"));
    params.push_back(make_unique<AudioParameterInt>("vectorLfoDepth", "vector LFO depth / wave-seq enable (0 = classic single-layer mode)", 0, 127, 0, "Depth"));
    // Wave-sequence crossfade fraction (0 = hard snap, 0.9 = very slow crossfade)
    params.push_back(make_unique<AudioParameterFloat>("waveSeqCrossfade", "wave sequence crossfade fraction", 0.0f, 0.9f, 0.3f));
    // Vector XY position (bilinear blend of 4 layers)
    params.push_back(make_unique<AudioParameterFloat>("vectorX", "Vector X", 0.0f, 1.0f, 0.5f));
    params.push_back(make_unique<AudioParameterFloat>("vectorY", "Vector Y", 0.0f, 1.0f, 0.5f));
    // Per-layer controls: Level (dB), Pan (-1..+1), Tune (cents fine-tune)
    static const char* layerNames[] = {"A", "B", "C", "D"};
    static const float tuneDefs[]   = {0.0f, 7.0f, 0.0f, -5.0f};
    for (int i = 0; i < 4; ++i) {
        String pfx = String("layer") + layerNames[i];
        params.push_back(make_unique<AudioParameterFloat>(pfx + "Level", pfx + " Level (dB)",
            NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.0f), 0.0f));
        params.push_back(make_unique<AudioParameterFloat>(pfx + "Pan", pfx + " Pan",
            -1.0f, 1.0f, 0.0f));
        params.push_back(make_unique<AudioParameterFloat>(pfx + "Tune", pfx + " Tune (cents)",
            -50.0f, 50.0f, tuneDefs[i]));
    }
    // Wave-sequence per-step preset selectors (arbitrary SF2 bank/preset per step)
    for (int i = 0; i < 4; ++i) {
        params.push_back(make_unique<AudioParameterInt>(
            String("step") + layerNames[i] + "Bank",
            String("Wave-seq step ") + layerNames[i] + " bank",
            MidiConstants::midiMinValue, 128, MidiConstants::midiMinValue));
        params.push_back(make_unique<AudioParameterInt>(
            String("step") + layerNames[i] + "Preset",
            String("Wave-seq step ") + layerNames[i] + " preset",
            MidiConstants::midiMinValue, MidiConstants::midiMaxValue, i));
    }

    return {
        make_move_iterator(params.begin()),
        make_move_iterator(params.end())
    };
}

JuicySFAudioProcessor::~JuicySFAudioProcessor()
{
}

void JuicySFAudioProcessor::initialiseSynth() {
    fluidSynthModel.initialise();
}

//==============================================================================
const String JuicySFAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool JuicySFAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool JuicySFAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

double JuicySFAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int JuicySFAudioProcessor::getNumPrograms()
{
    return fluidSynthModel.getNumPrograms();   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int JuicySFAudioProcessor::getCurrentProgram()
{
    return fluidSynthModel.getCurrentProgram();
}

void JuicySFAudioProcessor::setCurrentProgram(int index)
{
    fluidSynthModel.setCurrentProgram(index);
}

const String JuicySFAudioProcessor::getProgramName(int index)
{
    return fluidSynthModel.getProgramName(index);
}

void JuicySFAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void JuicySFAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    synth.setCurrentPlaybackSampleRate (sampleRate);
    keyboardState.reset();
    fluidSynthModel.setSampleRate(static_cast<float>(sampleRate));
    fluidSynthModel.prepareToPlay(samplesPerBlock);

    reset();
}

void JuicySFAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    keyboardState.reset();
}

bool JuicySFAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Only mono/stereo and input/output must have same layout
    const AudioChannelSet& mainOutput = layouts.getMainOutputChannelSet();
    const AudioChannelSet& mainInput  = layouts.getMainInputChannelSet();

    // input and output layout must either be the same or the input must be disabled altogether
    if (! mainInput.isDisabled() && mainInput != mainOutput)
        return false;

    // do not allow disabling the main buses
    if (mainOutput.isDisabled())
        return false;

    // only allow stereo and mono
    return mainOutput.size() <= 2;
}

AudioProcessor::BusesProperties JuicySFAudioProcessor::getBusesProperties() {
    return BusesProperties()
            .withOutput ("Output", AudioChannelSet::stereo(), true);
}

void JuicySFAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    jassert (!isUsingDoublePrecision());

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Now pass any incoming midi messages to our keyboard state object, and let it
    // add messages to the buffer if the user is clicking on the on-screen keys
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);
    
    fluidSynthModel.processBlock(buffer, midiMessages);

    // and now get our synth to process these midi events and generate its output.
    // synth.renderNextBlock(buffer, midiMessages, 0, numSamples);

    // (see juce_VST3_Wrapper.cpp for the assertion this would trip otherwise)
    // we are !JucePlugin_ProducesMidiOutput, so clear remaining MIDI messages from our buffer
    midiMessages.clear();
}

//==============================================================================
bool JuicySFAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* JuicySFAudioProcessor::createEditor()
{
    // grab a raw pointer to it for our own use
    return /*pluginEditor = */new JuicySFAudioProcessorEditor (*this, valueTreeState);
}

//==============================================================================
void JuicySFAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    // Create an outer XML element..
    XmlElement xml{"MYPLUGINSETTINGS"};

    // Store the values of all our parameters, using their param ID as the XML attribute
    XmlElement* params{xml.createNewChildElement("params")};
    for (auto* param : getParameters()) {
         if (auto* p = dynamic_cast<AudioProcessorParameterWithID*> (param)) {
             params->setAttribute(p->paramID, p->getValue());
         }
    }
    {
        ValueTree tree{valueTreeState.state.getChildWithName("uiState")};
        XmlElement* newElement{xml.createNewChildElement("uiState")};
        {
            double value{tree.getProperty("width", GuiConstants::minWidth)};
            newElement->setAttribute("width", value);
        }
        {
            double value{tree.getProperty("height", GuiConstants::minHeight)};
            newElement->setAttribute("height", value);
        }
    }
    {
        ValueTree tree{valueTreeState.state.getChildWithName("soundFont")};
        XmlElement* newElement{xml.createNewChildElement("soundFont")};
        {
            String value = tree.getProperty("path", "");
            newElement->setAttribute("path", value);
        }
        {
            MemoryBlock buffer;
            var value = tree.getProperty("bookmark", buffer);
            jassert(value.isBinaryData());
            newElement->setAttribute("bookmark", value.getBinaryData()->toBase64Encoding());
        }
    }
    
    DEBUG_PRINT(xml.createDocument("",false,false));
    
    copyXmlToBinary(xml, destData);
}

void JuicySFAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    // This getXmlFromBinary() helper function retrieves our XML from the binary blob..
    shared_ptr<XmlElement> xmlState{getXmlFromBinary(data, sizeInBytes)};
    DEBUG_PRINT(xmlState->createDocument("",false,false));
    
    if (xmlState.get() != nullptr) {
        // make sure that it's actually our type of XML object..
        if (xmlState->hasTagName(valueTreeState.state.getType())) {
            {
                XmlElement* xmlElement{xmlState->getChildByName("soundFont")};
                if (xmlElement) {
                    ValueTree tree{valueTreeState.state.getChildWithName("soundFont")};
                    {
                        Value value{tree.getPropertyAsValue("path", nullptr)};
                        value = xmlElement->getStringAttribute("path", value.getValue());
                    }
                    {
                        Value value{tree.getPropertyAsValue("bookmark", nullptr)};
                        jassert(value.getValue().isBinaryData());
                        MemoryBlock buffer;
                        buffer.fromBase64Encoding(xmlElement->getStringAttribute("bookmark", value.getValue()));
                        value = buffer;
                    }
                }
            }
            {
                ValueTree tree{valueTreeState.state.getChildWithName("uiState")};
                XmlElement* xmlElement{xmlState->getChildByName("uiState")};
                if (xmlElement) {
                    {
                        Value value{tree.getPropertyAsValue("width", nullptr)};
                        value = xmlElement->getIntAttribute("width", value.getValue());
                    }
                    {
                        Value value{tree.getPropertyAsValue("height", nullptr)};
                        value = xmlElement->getIntAttribute("height", value.getValue());
                    }
                }
            }
            XmlElement* params{xmlState->getChildByName("params")};
            if (params) {
                for (auto* param : getParameters()) {
                    if (auto* p = dynamic_cast<AudioProcessorParameterWithID*>(param)) {
                        p->setValueNotifyingHost(static_cast<float>(params->getDoubleAttribute(p->paramID, p->getValue())));
                    }
                }
            }
        }
    }
}

// FluidSynth only supports float in its process function, so that's all we can support.
bool JuicySFAudioProcessor::supportsDoublePrecisionProcessing() const {
    return false;
}

void JuicySFAudioProcessor::savePreset(const File& presetFile)
{
    String ini;
    ini << "; JuicySF preset\n";
    ini << "; https://github.com/probonopd/juicysfplugin\n";

    // --- [soundfont] section ---
    ini << "\n[soundfont]\n";
    String sf2Path = valueTreeState.state
                         .getChildWithName("soundFont")
                         .getProperty("path", "")
                         .toString();
    if (sf2Path.isNotEmpty()) {
        File sf2File(sf2Path);
        // Prefer a path relative to the preset file so the preset is portable
        String relPath = sf2File.getRelativePathFrom(presetFile.getParentDirectory());
        ini << "path=" << relPath << "\n";
    } else {
        ini << "path=\n";
    }

    // --- [params] section ---
    ini << "\n[params]\n";
    for (auto* param : getParameters()) {
        if (auto* p = dynamic_cast<AudioProcessorParameterWithID*>(param)) {
            if (auto* pi = dynamic_cast<AudioParameterInt*>(p)) {
                ini << p->paramID << "=" << pi->get() << "\n";
            } else if (auto* pf = dynamic_cast<AudioParameterFloat*>(p)) {
                ini << p->paramID << "=" << pf->get() << "\n";
            }
        }
    }

    presetFile.replaceWithText(ini);
}

void JuicySFAudioProcessor::loadPreset(const File& presetFile)
{
    if (!presetFile.existsAsFile())
        return;

    StringArray lines;
    presetFile.readLines(lines);

    String currentSection;
    juce::StringPairArray paramValues;
    String sf2PathRaw;

    for (const String& line : lines) {
        const String trimmed = line.trim();
        if (trimmed.isEmpty() || trimmed.startsWith(";"))
            continue;
        if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
            currentSection = trimmed.substring(1, trimmed.length() - 1).trim().toLowerCase();
            continue;
        }
        const int eqPos = trimmed.indexOfChar('=');
        if (eqPos < 0)
            continue;
        const String key   = trimmed.substring(0, eqPos).trim();
        const String value = trimmed.substring(eqPos + 1).trim();

        if (currentSection == "soundfont" && key == "path")
            sf2PathRaw = value;
        else if (currentSection == "params")
            paramValues.set(key, value);
    }

    // Restore parameters: parse saved actual value, convert to normalised 0..1
    for (auto* param : getParameters()) {
        if (auto* p = dynamic_cast<RangedAudioParameter*>(param)) {
            if (!paramValues.containsKey(p->paramID))
                continue;
            const float actual = paramValues[p->paramID].getFloatValue();
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(actual));
        }
    }

    // Restore soundfont path (resolve relative path from preset file directory)
    if (sf2PathRaw.isNotEmpty()) {
        File sf2File = File::isAbsolutePath(sf2PathRaw)
                           ? File(sf2PathRaw)
                           : presetFile.getParentDirectory().getChildFile(sf2PathRaw);
        if (sf2File.existsAsFile()) {
            Value pathValue = valueTreeState.state
                                  .getChildWithName("soundFont")
                                  .getPropertyAsValue("path", nullptr);
            pathValue.setValue(sf2File.getFullPathName());
        }
    }
}

FluidSynthModel& JuicySFAudioProcessor::getFluidSynthModel() {
    return fluidSynthModel;
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicySFAudioProcessor();
}
