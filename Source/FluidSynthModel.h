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
    // 4 logical layers, each backed by 2 physical MIDI channels (A/B) so that
    // PCM-level crossfade between consecutive wave-sequence steps is possible
    // without volume artefacts.
    static constexpr int numVectorLayers    = 4;   // logical layers (XY blend)
    static constexpr int numPhysChannels    = 8;   // 2 per layer: chanA + chanB
    static constexpr int numAudioGroups     = numPhysChannels;
    static constexpr int numScratchChannels = numAudioGroups * 2; // stereo per group
    static constexpr int numSeqSteps        = numVectorLayers;    // wave-seq length

    // -----------------------------------------------------------------------
    // Per-layer wave sequence state
    // -----------------------------------------------------------------------
    struct LayerSeqState {
        // Continuous position through the wave sequence (wraps at numSeqSteps).
        // Each layer starts offset from the previous by 1 step so they are always
        // at different timbres.
        float phase     {0.0f};

        // Which of the two physical MIDI channels is currently the "hot" (A) side?
        //   activeHalf==false  →  chanA = layer,              chanB = layer + numVectorLayers
        //   activeHalf==true   →  chanA = layer + numVectorLayers, chanB = layer
        bool  activeHalf{false};

        // PCM crossfade progress within the current wave-seq step.
        // 0 = chanA fully dominant, 1 = chanB fully dominant.
        float xfAlpha   {0.0f};
    };

    LayerSeqState layerStates[numVectorLayers];

    // -----------------------------------------------------------------------
    // Held-note tracking (used to re-trigger notes on the B channel at each
    // wave-sequence step transition).
    // -----------------------------------------------------------------------
    struct HeldNote { int note; int velocity; };
    std::vector<HeldNote> heldNotes;

    // -----------------------------------------------------------------------
    // Helper: physical MIDI channel indices for a logical layer
    // -----------------------------------------------------------------------
    int getChanA (int layer) const noexcept {
        return layerStates[layer].activeHalf
               ? layer + numVectorLayers
               : layer;
    }
    int getChanB (int layer) const noexcept {
        return layerStates[layer].activeHalf
               ? layer
               : layer + numVectorLayers;
    }

    // -----------------------------------------------------------------------
    // Wave-sequence state machine (called each processBlock)
    // -----------------------------------------------------------------------
    void advanceWaveSeq (int numSamples, int bank, int preset);

    // -----------------------------------------------------------------------
    // Vector synthesis: selects SF2 presets on all physical channels based on
    // the current wave-sequence step for each layer.
    // -----------------------------------------------------------------------
    void selectAllLayerPresets (int bank, int preset);

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

    // Per-layer parameters (index: 0=A, 1=B, 2=C, 3=D)
    AudioParameterFloat* layerLevelParam[numVectorLayers]{};
    AudioParameterFloat* layerPanParam  [numVectorLayers]{};
    AudioParameterFloat* layerTuneParam [numVectorLayers]{};

    // Scratch buffer: numScratchChannels mono channels, each length = maxBlockSize.
    // fluid_synth_process writes one stereo pair per audio group into these buffers.
    juce::AudioBuffer<float> scratchBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidSynthModel)
};
