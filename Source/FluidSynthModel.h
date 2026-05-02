//
// Created by Alex Birch on 10/09/2017.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <fluidsynth.h>
#include <memory>
#include <map>
#include "MidiConstants.h"

using namespace std;

class FluidSynthModel
: public ValueTree::Listener
, public AudioProcessorValueTreeState::Listener {
public:
    FluidSynthModel(
        AudioProcessorValueTreeState& valueTreeState
        );
     ~FluidSynthModel();

    void initialise();

    /** Must be called from prepareToPlay() before the first processBlock(). */
    void prepareToPlay(int maxBlockSize);

    int getChannel();

    void setControllerValue(int controller, int value);

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages);

    void setSampleRate(float sampleRate);
    
    //==============================================================================
    virtual void parameterChanged (const String& parameterID, float newValue) override;
    
    virtual void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                           const Identifier& property) override;
    inline virtual void valueTreeChildAdded (ValueTree& parentTree,
                                             ValueTree& childWhichHasBeenAdded) override {};
    inline virtual void valueTreeChildRemoved (ValueTree& parentTree,
                                               ValueTree& childWhichHasBeenRemoved,
                                               int indexFromWhichChildWasRemoved) override {};
    inline virtual void valueTreeChildOrderChanged (ValueTree& parentTreeWhoseChildrenHaveMoved,
                                                    int oldIndex, int newIndex) override {};
    inline virtual void valueTreeParentChanged (ValueTree& treeWhoseParentHasChanged) override {};
    inline virtual void valueTreeRedirected (ValueTree& treeWhichHasBeenChanged) override {};

    //==============================================================================
    int getNumPrograms();
    int getCurrentProgram();
    void setCurrentProgram(int index);
    const String getProgramName(int index);
    void changeProgramName(int index, const String& newName);

private:
    static const StringArray programChangeParams;

    // there's no bimap in the standard library!
    static const map<fluid_midi_control_change, String> controllerToParam;
    static const map<String, fluid_midi_control_change> paramToController;

    void refreshBanks();

    AudioProcessorValueTreeState& valueTreeState;

    // https://stackoverflow.com/questions/38980315/is-stdunique-ptr-deletion-order-guaranteed
    // members are destroyed in reverse of the order they're declared
    // http://www.fluidsynth.org/api/
    // in their examples, they destroy the synth before destroying the settings
    unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings;
    unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth;

    float currentSampleRate;

    void unloadAndLoadFont(const String &absPath);
    void loadFont(const String &absPath);

    // -----------------------------------------------------------------------
    // Wave-sequence / vector synthesis constants
    // -----------------------------------------------------------------------
    // 4 voice slots, each backed by 2 physical MIDI channels (A/B) so that
    // PCM-level crossfade between consecutive wave-sequence steps is possible.
    // Channel layout: voice V uses channels V (chanA) and V+MAX_VOICES (chanB).
    static constexpr int MAX_VOICES      = 4;
    static constexpr int numPhysChannels = MAX_VOICES * 2; // 8 total
    static constexpr int numAudioGroups  = numPhysChannels;
    static constexpr int numScratchChannels = numAudioGroups * 2; // stereo per group
    static constexpr int numSeqSteps     = 4;  // wave-sequence steps A/B/C/D

    // -----------------------------------------------------------------------
    // Per-voice wave sequence state (one entry per voice slot)
    // -----------------------------------------------------------------------
    struct VoiceState {
        bool  active     {false}; // note is currently held
        int   noteNumber {0};
        int   velocity   {0};

        // Continuous position through the wave sequence (wraps at numSeqSteps).
        // Independent per voice — starts at 0 when the note is triggered.
        float phase      {0.0f};

        // Which of the two physical MIDI channels is currently the "hot" (A) side?
        //   activeHalf==false  → chanA = voiceSlot,             chanB = voiceSlot + MAX_VOICES
        //   activeHalf==true   → chanA = voiceSlot + MAX_VOICES, chanB = voiceSlot
        bool  activeHalf {false};

        // PCM crossfade progress within the current wave-seq step.
        // 0 = chanA fully dominant, 1 = chanB fully dominant.
        float xfAlpha    {0.0f};
    };

    VoiceState voices[MAX_VOICES];

    // -----------------------------------------------------------------------
    // Helper: physical MIDI channel indices for a voice slot
    // -----------------------------------------------------------------------
    int getChanA (int vs) const noexcept {
        return voices[vs].activeHalf ? vs + MAX_VOICES : vs;
    }
    int getChanB (int vs) const noexcept {
        return voices[vs].activeHalf ? vs : vs + MAX_VOICES;
    }

    // -----------------------------------------------------------------------
    // Wave-sequence state machine (called each processBlock)
    // -----------------------------------------------------------------------
    void advanceWaveSeq (int numSamples);

    // -----------------------------------------------------------------------
    // Load step presets onto all physical channels (called after font load /
    // step preset parameter change).
    // -----------------------------------------------------------------------
    void loadStepPresetsOnChannels();

    /** Compute per-layer PCM gain values for this audio block.
     *  depth==0 → single-layer compat (gains[0]=1, rest 0).
     *  depth>0  → XY bilinear equal-power mix, optionally swept by LFO. */
    void computeLayerGains (int numSamples, float* gains);

    /** Push the current tune parameter value into FluidSynth for the given layer
     *  (applies to both chanA and chanB physical channels). */
    void applyLayerTune (int layer);

    int sfont_id;
    unsigned int channel;

    float vectorLfoPhase{0.0f};
    AudioParameterFloat* vectorLfoRateParam  {nullptr};
    AudioParameterInt*   vectorLfoDepthParam {nullptr};
    AudioParameterFloat* vectorXParam        {nullptr};
    AudioParameterFloat* vectorYParam        {nullptr};
    AudioParameterFloat* waveSeqCrossfadeParam{nullptr};

    // Per-layer parameters (index: 0=A, 1=B, 2=C, 3=D) — still used in legacy mode
    AudioParameterFloat* layerLevelParam[MAX_VOICES]{};
    AudioParameterFloat* layerPanParam  [MAX_VOICES]{};
    AudioParameterFloat* layerTuneParam [MAX_VOICES]{};

    // Per-step wave-sequence preset selectors
    AudioParameterInt* stepBankParam  [numSeqSteps]{};
    AudioParameterInt* stepPresetParam[numSeqSteps]{};

    // Scratch buffer: numScratchChannels mono channels, each length = maxBlockSize.
    // fluid_synth_process writes one stereo pair per audio group into these buffers.
    juce::AudioBuffer<float> scratchBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidSynthModel)
};
