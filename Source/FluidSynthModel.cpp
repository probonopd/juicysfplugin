//
// Created by Alex Birch on 10/09/2017.
//

#include <iostream>
#include <iterator>
#include <cmath>
#include <fluidsynth.h>
#include "FluidSynthModel.h"
#include "MidiConstants.h"
#include "Util.h"

#if JUCE_MAC || JUCE_IOS
  #include <juce_core/native/juce_mac_CFHelpers.h>
  #include <CoreFoundation/CFString.h>
  #include <CoreFoundation/CFData.h>
  #include <CoreFoundation/CFURL.h>
  #include <CoreFoundation/CFError.h>
  using juce::CFUniquePtr;
#endif

using namespace std;

const map<fluid_midi_control_change, String> FluidSynthModel::controllerToParam{
    {SOUND_CTRL2, "filterResonance"}, // MIDI CC 71 Timbre/Harmonic Intensity (filter resonance)
    {SOUND_CTRL3, "release"}, // MIDI CC 72 Release time
    {SOUND_CTRL4, "attack"}, // MIDI CC 73 Attack time
    {SOUND_CTRL5, "filterCutOff"}, // MIDI CC 74 Brightness (cutoff frequency, FILTERFC)
    {SOUND_CTRL6, "decay"}, // MIDI CC 75 Decay Time
    {SOUND_CTRL10, "sustain"}}; // MIDI CC 79 undefined

const map<String, fluid_midi_control_change> FluidSynthModel::paramToController{[]{
    map<String, fluid_midi_control_change> map;
    transform(
        controllerToParam.begin(),
        controllerToParam.end(),
        inserter(map, map.begin()),
        [](const pair<fluid_midi_control_change, String>& pair) {
            return make_pair(pair.second, pair.first);
        });
    return map;
}()};

FluidSynthModel::FluidSynthModel(
    AudioProcessorValueTreeState& valueTreeState
    )
: valueTreeState{valueTreeState}
, settings{nullptr, nullptr}
, synth{nullptr, nullptr}
, currentSampleRate{44100}
, sfont_id{-1}
, channel{0}
{
    valueTreeState.addParameterListener("bank", this);
    valueTreeState.addParameterListener("preset", this);
    for (const auto &[param, controller]: paramToController) {
        valueTreeState.addParameterListener(param, this);
    }
    // Listen for per-layer tune changes so we can push them to FluidSynth
    static const char* lNames[] = {"A", "B", "C", "D"};
    for (int li = 0; li < numVectorLayers; ++li) {
        valueTreeState.addParameterListener(String("layer") + lNames[li] + "Tune", this);
    }
    valueTreeState.state.addListener(this);
}

FluidSynthModel::~FluidSynthModel() {
    for (const auto &[param, controller]: paramToController) {
        valueTreeState.removeParameterListener(param, this);
    }
    static const char* lNames[] = {"A", "B", "C", "D"};
    for (int li = 0; li < numVectorLayers; ++li) {
        valueTreeState.removeParameterListener(String("layer") + lNames[li] + "Tune", this);
    }
    valueTreeState.removeParameterListener("bank", this);
    valueTreeState.removeParameterListener("preset", this);
    valueTreeState.state.removeListener(this);
}

void FluidSynthModel::initialise() {
    // deactivate all audio drivers in fluidsynth to avoid FL Studio deadlock when initialising CoreAudio
    // after all: we only use fluidsynth to render blocks of audio. it doesn't output to audio driver.
    const char *DRV[] {NULL};
    fluid_audio_driver_register(DRV);
    
    settings = { new_fluid_settings(), delete_fluid_settings };
    
    // https://sourceforge.net/p/fluidsynth/wiki/FluidSettings/
#if JUCE_DEBUG
    fluid_settings_setint(settings.get(), "synth.verbose", 1);
#endif

    // Enable per-layer stereo output: each MIDI channel maps to its own audio group.
    // Channel i → group (i % numAudioGroups) → output pair (2i, 2i+1).
    // MUST be set before new_fluid_synth().
    fluid_settings_setint(settings.get(), "synth.audio-channels", numAudioGroups);
    fluid_settings_setint(settings.get(), "synth.audio-groups",   numAudioGroups);

    synth = { new_fluid_synth(settings.get()), delete_fluid_synth };
    fluid_synth_set_sample_rate(synth.get(), currentSampleRate);

    // I can't hear a damned thing
    fluid_synth_set_gain(synth.get(), 2.0);
    
    // note: fluid_chan.c#fluid_channel_init_ctrl()
    // > Just like panning, a value of 64 indicates no change for sound ctrls
    // --
    // so, advice is to leave everything at 64
    // and yet, I'm finding that default modulators start at MIN,
    // i.e. we are forced to start at 0 and climb from there
    // --
    // let's zero out every audio param that we manage
    for (const auto &[controller, param]: controllerToParam) {
        setControllerValue(static_cast<int>(controller), 0);
    }
    
    // http://www.synthfont.com/SoundFont_NRPNs.PDF
    float env_amount{20000.0f};
    
    unique_ptr<fluid_mod_t, decltype(&delete_fluid_mod)> mod{new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL2), // MIDI CC 71 Timbre/Harmonic Intensity (filter resonance)
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_CONCAVE
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_FILTERQ);
    fluid_mod_set_amount(mod.get(), FLUID_PEAK_ATTENUATION);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    // Release: BIPOLAR so that CC=0 (default, slider at bottom) gives a fast release
    // (subtracts 12000 tc from the SF2 value), CC=64 keeps the SF2 natural release,
    // and CC=127 extends it by 12000 tc.
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL3), // MIDI CC 72 Release time
                          FLUID_MOD_CC
                          | FLUID_MOD_BIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVRELEASE);
    fluid_mod_set_amount(mod.get(), 12000.0f);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL4), // MIDI CC 73 Attack time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVATTACK);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    // soundfont spec says that if cutoff is >20kHz and resonance Q is 0, then no filtering occurs
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL5), // MIDI CC 74 Brightness (cutoff frequency, FILTERFC)
                          FLUID_MOD_CC
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_POSITIVE);
        fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_FILTERFC);
    fluid_mod_set_amount(mod.get(), -2400.0f);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL6), // MIDI CC 75 Decay Time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVDECAY);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL10), // MIDI CC 79 undefined
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_CONCAVE
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVSUSTAIN);
    // fluice_voice.c#fluid_voice_update_param()
    // clamps the range to between 0 and 1000, so we'll copy that
    fluid_mod_set_amount(mod.get(), 1000.0f);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);

    // Cache vector parameter pointers for lock-free access on the audio thread
    vectorLfoRateParam   = dynamic_cast<AudioParameterFloat*>(valueTreeState.getParameter("vectorLfoRate"));
    vectorLfoDepthParam  = dynamic_cast<AudioParameterInt*>  (valueTreeState.getParameter("vectorLfoDepth"));
    vectorXParam         = dynamic_cast<AudioParameterFloat*>(valueTreeState.getParameter("vectorX"));
    vectorYParam         = dynamic_cast<AudioParameterFloat*>(valueTreeState.getParameter("vectorY"));
    waveSeqCrossfadeParam= dynamic_cast<AudioParameterFloat*>(valueTreeState.getParameter("waveSeqCrossfade"));

    static const char* lNames[] = {"A", "B", "C", "D"};
    for (int li = 0; li < numVectorLayers; ++li) {
        String pfx = String("layer") + lNames[li];
        layerLevelParam[li] = dynamic_cast<AudioParameterFloat*>(valueTreeState.getParameter(pfx + "Level"));
        layerPanParam[li]   = dynamic_cast<AudioParameterFloat*>(valueTreeState.getParameter(pfx + "Pan"));
        layerTuneParam[li]  = dynamic_cast<AudioParameterFloat*>(valueTreeState.getParameter(pfx + "Tune"));
    }

    // Initialise wave-sequence layer states: each layer is offset by 1 step so
    // all four presets are always active simultaneously with different blend weights.
    for (int li = 0; li < numVectorLayers; ++li) {
        layerStates[li].phase      = static_cast<float>(li); // 0, 1, 2, 3
        layerStates[li].activeHalf = false;
        layerStates[li].xfAlpha    = 0.0f;
    }
    heldNotes.reserve(32);

    // Push initial per-layer fine-tuning into FluidSynth
    for (int li = 0; li < numVectorLayers; ++li)
        applyLayerTune(li);

    // Pre-allocate scratch buffer so processBlock never allocates on the audio thread
    scratchBuffer.setSize(numScratchChannels, 4096, false, true, false);
}

const StringArray FluidSynthModel::programChangeParams{"bank", "preset"};
void FluidSynthModel::parameterChanged(const String& parameterID, float newValue) {
    if (programChangeParams.contains(parameterID)) {
        int bank, preset;
        {
            RangedAudioParameter *param{valueTreeState.getParameter("bank")};
            jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
            AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
            bank = castParam->get();
        }
        {
            RangedAudioParameter *param{valueTreeState.getParameter("preset")};
            jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
            AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
            preset = castParam->get();
        }
        // selectAllLayerPresets handles ALL physical channels (0-7) based on the
        // current wave-sequence step for each layer, including channel 0.
        selectAllLayerPresets(bank, preset);
    } else if (
        auto it{paramToController.find(parameterID)};
        it != end(paramToController)) {
        RangedAudioParameter *param{valueTreeState.getParameter(parameterID)};
        jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
        AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
        int value{castParam->get()};
        int controllerNumber{static_cast<int>(it->second)};
        // Forward envelope/filter CCs to ALL physical channels so every layer
        // (including crossfade B-side channels) shares the same ADSR/filter.
        for (int phys = 0; phys < numPhysChannels; ++phys) {
            fluid_synth_cc(synth.get(), phys, controllerNumber, value);
        }
    } else {
        // Check for per-layer tune parameter changes
        static const char* lNames[] = {"A", "B", "C", "D"};
        for (int li = 0; li < numVectorLayers; ++li) {
            if (parameterID == String("layer") + lNames[li] + "Tune") {
                applyLayerTune(li);
                break;
            }
        }
    }
}

void FluidSynthModel::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                               const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")) {
#if JUCE_MAC || JUCE_IOS
        if (property == StringRef("bookmark")) {
            CFErrorRef* cfError;
            MemoryBlock buffer;
            var bookmark = treeWhosePropertyHasChanged.getProperty("bookmark", buffer);
            jassert(bookmark.isBinaryData());
            CFUniquePtr<CFDataRef> data{CFDataCreate(
                NULL,
                static_cast<const UInt8 *>(bookmark.getBinaryData()->getData()),
                static_cast<CFIndex>(bookmark.getBinaryData()->getSize()))};
            CFUniquePtr<CFURLRef> cfURL{CFURLCreateByResolvingBookmarkData(NULL, data.get(), kCFURLBookmarkResolutionWithSecurityScope, NULL, NULL, NULL, cfError)};

            CFUniquePtr<CFStringRef> cfPath {CFURLCopyFileSystemPath(cfURL.get(), CFURLPathStyle::kCFURLPOSIXPathStyle)};
            StringRef path {String::fromCFString(cfPath.get())};
            if (path.isNotEmpty()) {
                CFURLStartAccessingSecurityScopedResource(cfURL.get());
                unloadAndLoadFont(path);
                CFURLStopAccessingSecurityScopedResource(cfURL.get());
            }
        }
#else
        if (property == StringRef("path")) {
            String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
            if (soundFontPath.isNotEmpty()) {
                unloadAndLoadFont(soundFontPath);
            }
        }
#endif
    }
}

void FluidSynthModel::setControllerValue(int controller, int value) {
    // Forward to all physical channels so every layer has consistent controller state
    for (int phys = 0; phys < numPhysChannels; ++phys) {
        fluid_synth_cc(synth.get(), phys, controller, value);
    }
}

int FluidSynthModel::getChannel() {
    return channel;
}

void FluidSynthModel::unloadAndLoadFont(const String &absPath) {
    // in the base case, there is no font loaded
    if (fluid_synth_sfcount(synth.get()) > 0) {
        // if -1 is returned, that indicates failure
        // not really sure how to handle "fail to unload"
        fluid_synth_sfunload(synth.get(), sfont_id, 1);
        sfont_id = -1;
    }
    loadFont(absPath);
}

void FluidSynthModel::loadFont(const String &absPath) {
    if (!absPath.isEmpty()) {
        sfont_id = fluid_synth_sfload(synth.get(), absPath.toStdString().c_str(), 1);
        // if -1 is returned, that indicates failure
    }
    // refresh regardless of success, if only to clear the table
    refreshBanks();
    // After loading a soundfont, seed all vector layer channels with their presets
    if (sfont_id != -1) {
        int bank = 0, preset = 0;
        if (auto* p = dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("bank")))
            bank = p->get();
        if (auto* p = dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("preset")))
            preset = p->get();
        selectAllLayerPresets(bank, preset);
    }
}

void FluidSynthModel::refreshBanks() {
    ValueTree banks{"banks"};
    fluid_sfont_t* sfont{
        sfont_id == -1
        ? nullptr
        : fluid_synth_get_sfont_by_id(synth.get(), sfont_id)
    };
    if (sfont) {
        int greatestEncounteredBank{-1};
        ValueTree bank;

        fluid_sfont_iteration_start(sfont);
        for(fluid_preset_t* preset {fluid_sfont_iteration_next(sfont)};
        preset != nullptr;
        preset = fluid_sfont_iteration_next(sfont)) {
            int bankNum{fluid_preset_get_banknum(preset)};
            if (bankNum > greatestEncounteredBank) {
                if (greatestEncounteredBank > -1) {
                    banks.appendChild(bank, nullptr);
                }
                bank = { "bank", {
                    { "num", bankNum }
                } };
                greatestEncounteredBank = bankNum;
            }
            bank.appendChild({ "preset", {
                { "num", fluid_preset_get_num(preset) },
                { "name", String{fluid_preset_get_name(preset)} }
            }, {} }, nullptr);
        }
        if (greatestEncounteredBank > -1) {
            banks.appendChild(bank, nullptr);
        }
    }
    valueTreeState.state.getChildWithName("banks").copyPropertiesAndChildrenFrom(banks, nullptr);
    valueTreeState.state.getChildWithName("banks").sendPropertyChangeMessage("synthetic");
    
#if JUCE_DEBUG
//    unique_ptr<XmlElement> xml{valueTreeState.state.createXml()};
//    Logger::outputDebugString(xml->createDocument("",false,false));
#endif
}

void FluidSynthModel::setSampleRate(float sampleRate) {
    currentSampleRate = sampleRate;
    // https://stackoverflow.com/a/40856043/5257399
    // test if a smart pointer is null
    if (!synth) {
        // don't worry; we'll do this in initialise phase regardless
        return;
    }
    fluid_synth_set_sample_rate(synth.get(), sampleRate);
}

void FluidSynthModel::selectAllLayerPresets(int bank, int preset) {
    if (sfont_id == -1) return;
    int bankOffset{fluid_synth_get_bank_offset(synth.get(), sfont_id)};
    // For each logical layer, set the preset for BOTH its A and B physical channels
    // based on the current wave-sequence step stored in layerStates.
    for (int li = 0; li < numVectorLayers; ++li) {
        const int stepA = static_cast<int>(std::floor(layerStates[li].phase)) % numSeqSteps;
        const int stepB = (stepA + 1) % numSeqSteps;
        const int presetA = (preset + stepA) % 128;
        const int presetB = (preset + stepB) % 128;
        const int chanA   = getChanA(li);
        const int chanB   = getChanB(li);
        fluid_synth_program_select(synth.get(), chanA, sfont_id,
            static_cast<unsigned int>(bankOffset + bank),
            static_cast<unsigned int>(presetA));
        fluid_synth_program_select(synth.get(), chanB, sfont_id,
            static_cast<unsigned int>(bankOffset + bank),
            static_cast<unsigned int>(presetB));
    }
}

void FluidSynthModel::applyLayerTune(int layer) {
    if (!synth || layer < 0 || layer >= numVectorLayers) return;
    const float cents = layerTuneParam[layer] ? layerTuneParam[layer]->get() : 0.0f;
    // Apply to both physical channels of this logical layer
    fluid_synth_set_gen(synth.get(), layer,                  GEN_FINETUNE, cents);
    fluid_synth_set_gen(synth.get(), layer + numVectorLayers, GEN_FINETUNE, cents);
}

void FluidSynthModel::prepareToPlay(int maxBlockSize) {
    // Pre-allocate scratch buffer to avoid any audio-thread allocation
    if (maxBlockSize > scratchBuffer.getNumSamples() || scratchBuffer.getNumChannels() < numScratchChannels)
        scratchBuffer.setSize(numScratchChannels, juce::jmax(maxBlockSize, 4096), false, true, false);
}

void FluidSynthModel::computeLayerGains(int numSamples, float* gains) {
    const int depth = vectorLfoDepthParam ? vectorLfoDepthParam->get() : 0;

    if (depth == 0) {
        // Single-layer compatibility: only layer 0 is active
        gains[0] = 1.0f;
        for (int i = 1; i < numVectorLayers; ++i) gains[i] = 0.0f;
        return;
    }

    // Advance LFO phase
    const float rate = vectorLfoRateParam ? vectorLfoRateParam->get() : 0.0f;
    if (rate > 0.0f && currentSampleRate > 0.0f) {
        const float twoPi = juce::MathConstants<float>::twoPi;
        vectorLfoPhase += twoPi * rate * static_cast<float>(numSamples) / currentSampleRate;
        while (vectorLfoPhase >= twoPi) vectorLfoPhase -= twoPi;
    }

    // Effective XY: static position swept by LFO in a circle.
    // d controls how much the LFO moves the position away from the user-set X/Y.
    const float d     = depth / 127.0f;
    const float baseX = vectorXParam ? vectorXParam->get() : 0.5f;
    const float baseY = vectorYParam ? vectorYParam->get() : 0.5f;
    const float lfoX  = 0.5f + 0.5f * std::sin(vectorLfoPhase);
    const float lfoY  = 0.5f + 0.5f * std::cos(vectorLfoPhase);
    const float effX  = juce::jlimit(0.0f, 1.0f, baseX * (1.0f - d) + lfoX * d);
    const float effY  = juce::jlimit(0.0f, 1.0f, baseY * (1.0f - d) + lfoY * d);

    // Bilinear partition of unity → equal-power via sqrt.
    // Corner mapping: A(0,0) B(1,0) C(1,1) D(0,1)
    // At any position sum(wX) == 1, so sum of squares of gains == 1 (constant power).
    const float wA = (1.0f - effX) * (1.0f - effY);
    const float wB =         effX  * (1.0f - effY);
    const float wC =         effX  *         effY;
    const float wD = (1.0f - effX) *         effY;
    gains[0] = std::sqrt(wA);
    gains[1] = std::sqrt(wB);
    gains[2] = std::sqrt(wC);
    gains[3] = std::sqrt(wD);
}

// ---------------------------------------------------------------------------
// Wave-sequence state machine
// ---------------------------------------------------------------------------
// Called once per processBlock AFTER MIDI events have been processed into
// heldNotes.  Advances the continuous phase for each logical layer and, when
// a step boundary is crossed, performs the step transition:
//   1. Send note-off to old chanA (now at gain≈0 in the PCM mix)
//   2. Swap activeHalf → old chanB becomes new chanA
//   3. Select the next-next preset on new chanB (old chanA)
//   4. Re-trigger all held notes on new chanB so it is ready to fade in
// ---------------------------------------------------------------------------
void FluidSynthModel::advanceWaveSeq(int numSamples, int bank, int preset) {
    if (sfont_id == -1) return;

    const float rate  = vectorLfoRateParam    ? vectorLfoRateParam->get()    : 0.5f;
    const float cfrac = waveSeqCrossfadeParam ? waveSeqCrossfadeParam->get() : 0.3f;

    // Phase advance per block (in steps/block)
    const float dt = (rate > 0.0f && currentSampleRate > 0.0f)
                     ? rate * static_cast<float>(numSamples) / currentSampleRate
                     : 0.0f;

    const int bankOffset = fluid_synth_get_bank_offset(synth.get(), sfont_id);

    for (int li = 0; li < numVectorLayers; ++li) {
        auto& st = layerStates[li];

        const float prevPhase = st.phase;
        st.phase += dt;

        // Did we cross a step boundary?
        const bool stepAdvanced =
            (static_cast<int>(std::floor(st.phase)) >
             static_cast<int>(std::floor(prevPhase)));

        if (stepAdvanced) {
            // Step transition ------------------------------------------------
            const int oldChanA = getChanA(li); // will become new chanB

            // 1. Send note-off to old chanA (currently at xfAlpha≈1, so gain≈0)
            for (const auto& hn : heldNotes)
                fluid_synth_noteoff(synth.get(), oldChanA, hn.note);

            // 2. Swap: old chanB becomes new chanA, old chanA becomes new chanB
            st.activeHalf = !st.activeHalf;

            // 3. Determine the NEXT-NEXT step (2 ahead of where we just landed)
            const int curStep      = static_cast<int>(std::floor(st.phase)) % numSeqSteps;
            const int nextNextStep = (curStep + 1) % numSeqSteps;
            const int nnPreset     = (preset + nextNextStep) % 128;

            const int newChanB = getChanB(li); // = old chanA after the swap
            fluid_synth_program_select(
                synth.get(), newChanB, sfont_id,
                static_cast<unsigned int>(bankOffset + bank),
                static_cast<unsigned int>(nnPreset));

            // 4. Pre-trigger all held notes on new chanB so it is in sustain
            //    by the time the next crossfade starts (≥ (1-cfrac) of a step later)
            for (const auto& hn : heldNotes)
                fluid_synth_noteon(synth.get(), newChanB, hn.note, hn.velocity);

            // Reset crossfade to HOLD state
            st.xfAlpha = 0.0f;
        }

        // Recompute xfAlpha from the current fractional step position
        const float stepFrac = st.phase - std::floor(st.phase);
        if (cfrac > 0.0f && stepFrac > (1.0f - cfrac)) {
            st.xfAlpha = juce::jlimit(0.0f, 1.0f,
                             (stepFrac - (1.0f - cfrac)) / cfrac);
        } else {
            st.xfAlpha = 0.0f;
        }
    }
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    const int numSamples = buffer.getNumSamples();
    int time;
    MidiMessage m;

    // Compute per-layer XY bilinear gains for this block (also advances LFO phase)
    float gains[numVectorLayers];
    computeLayerGains(numSamples, gains);

    // Is the wave-sequence / vector mode active?
    const bool waveSeqActive = vectorLfoDepthParam && (vectorLfoDepthParam->get() > 0);

    // --- Process MIDI events ------------------------------------------------
    for (MidiBuffer::Iterator i{midiMessages}; i.getNextEvent(m, time);) {
        DEBUG_PRINT(m.getDescription());

        if (m.isNoteOn()) {
            const int note = m.getNoteNumber();
            const int vel  = m.getVelocity();

            // Track the held note so advanceWaveSeq can re-trigger it at step transitions
            heldNotes.push_back({note, vel});

            if (waveSeqActive) {
                // Trigger on ALL physical channels (chanA + chanB for every layer).
                // chanB voices are at gain=0 in the PCM mix until a crossfade begins,
                // but pre-loading them now means they are already in sustain phase
                // when their crossfade eventually starts — giving smooth timbre blends.
                for (int phys = 0; phys < numPhysChannels; ++phys)
                    fluid_synth_noteon(synth.get(), phys, note, vel);
            } else {
                // Single-layer compat: only logical channel 0
                fluid_synth_noteon(synth.get(), 0, note, vel);
            }

        } else if (m.isNoteOff()) {
            const int note = m.getNoteNumber();
            heldNotes.erase(
                std::remove_if(heldNotes.begin(), heldNotes.end(),
                               [note](const HeldNote& h) { return h.note == note; }),
                heldNotes.end());
            // Always send note-off to every physical channel to prevent stuck voices
            // if the user toggles wave-seq mode while holding notes.
            for (int phys = 0; phys < numPhysChannels; ++phys)
                fluid_synth_noteoff(synth.get(), phys, note);

        } else if (m.isController()) {
            // Incoming MIDI CC → forward to all physical channels
            for (int phys = 0; phys < numPhysChannels; ++phys)
                fluid_synth_cc(synth.get(), phys, m.getControllerNumber(), m.getControllerValue());

            fluid_midi_control_change controllerNum{
                static_cast<fluid_midi_control_change>(m.getControllerNumber())};
            if (auto it{controllerToParam.find(controllerNum)};
                it != end(controllerToParam)) {
                String parameterID{it->second};
                RangedAudioParameter *param{valueTreeState.getParameter(parameterID)};
                jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
                AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
                *castParam = m.getControllerValue();
            }
        } else if (m.isProgramChange()) {
#if JUCE_DEBUG
            String debug{"MIDI program change: "};
            debug << m.getProgramChangeNumber();
            Logger::outputDebugString(debug);
#endif
            int result{fluid_synth_program_change(
                synth.get(), channel, m.getProgramChangeNumber())};
            if (result == FLUID_OK) {
                RangedAudioParameter *param{valueTreeState.getParameter("preset")};
                jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
                AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
                *castParam = m.getProgramChangeNumber();
            }
        } else if (m.isPitchWheel()) {
            // Pitch wheel to all physical channels for uniform bending across layers
            for (int phys = 0; phys < numPhysChannels; ++phys)
                fluid_synth_pitch_bend(synth.get(), phys, m.getPitchWheelValue());
        } else if (m.isChannelPressure()) {
            for (int phys = 0; phys < numPhysChannels; ++phys)
                fluid_synth_channel_pressure(synth.get(), phys, m.getChannelPressureValue());
        } else if (m.isAftertouch()) {
            for (int phys = 0; phys < numPhysChannels; ++phys)
                fluid_synth_key_pressure(synth.get(), phys, m.getNoteNumber(), m.getAfterTouchValue());
        } else if (m.isSysEx()) {
            fluid_synth_sysex(
                synth.get(),
                reinterpret_cast<const char*>(m.getSysExData()),
                m.getSysExDataSize(),
                nullptr, nullptr, nullptr,
                static_cast<int>(false));
        }
    }

    // --- Advance wave-sequence state machine --------------------------------
    if (waveSeqActive) {
        int bank = 0, preset = 0;
        if (auto* p = dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("bank")))
            bank = p->get();
        if (auto* p = dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("preset")))
            preset = p->get();
        advanceWaveSeq(numSamples, bank, preset);
    }

    // --- PCM-level mixing ---------------------------------------------------
    // Grow scratch buffer if the host sends a larger block than expected (rare)
    if (scratchBuffer.getNumSamples() < numSamples ||
        scratchBuffer.getNumChannels() < numScratchChannels)
        scratchBuffer.setSize(numScratchChannels, numSamples, false, true, false);

    // Zero all scratch channels (fluid_synth_process writes/adds into them)
    for (int c = 0; c < numScratchChannels; ++c)
        scratchBuffer.clear(c, 0, numSamples);

    // Build pointer array for all 8 audio groups × 2 stereo channels = 16 pointers
    float* outPtrs[numScratchChannels];
    for (int c = 0; c < numScratchChannels; ++c)
        outPtrs[c] = scratchBuffer.getWritePointer(c);

    fluid_synth_process(synth.get(), numSamples, 0, nullptr, numScratchChannels, outPtrs);

    // --- Mix 4 logical layers (each with A + B channel) into output ---------
    buffer.clear();
    const int numOutChannels = buffer.getNumChannels();

    for (int li = 0; li < numVectorLayers; ++li) {
        const float layerGain = gains[li];
        const float xfAlpha   = waveSeqActive ? layerStates[li].xfAlpha : 0.0f;

        const float gA = layerGain * (1.0f - xfAlpha);
        const float gB = layerGain * xfAlpha;

        if (gA < 1e-6f && gB < 1e-6f) continue;

        // Per-layer level (dB) and constant-power pan
        float levelGain = 1.0f;
        if (layerLevelParam[li])
            levelGain = juce::Decibels::decibelsToGain(layerLevelParam[li]->get());

        float panL = 1.0f, panR = 1.0f;
        if (layerPanParam[li]) {
            const float pan = layerPanParam[li]->get(); // -1.0 .. +1.0
            panL = std::sqrt(0.5f * (1.0f - pan));
            panR = std::sqrt(0.5f * (1.0f + pan));
        }

        // Identify the physical channels for this layer
        const int chanA_phys = getChanA(li);
        const int chanB_phys = getChanB(li);

        // Helper lambda: accumulate one physical channel into the output buffer
        auto mixChannel = [&](int physCh, float gain) {
            if (gain < 1e-6f) return;
            const float totalL = gain * levelGain * panL;
            const float totalR = gain * levelGain * panR;
            const float* srcL  = scratchBuffer.getReadPointer(physCh * 2);
            const float* srcR  = scratchBuffer.getReadPointer(physCh * 2 + 1);
            if (numOutChannels >= 2) {
                float* dstL = buffer.getWritePointer(0);
                float* dstR = buffer.getWritePointer(1);
                for (int s = 0; s < numSamples; ++s) {
                    dstL[s] += srcL[s] * totalL;
                    dstR[s] += srcR[s] * totalR;
                }
            } else {
                float* dst = buffer.getWritePointer(0);
                for (int s = 0; s < numSamples; ++s)
                    dst[s] += (srcL[s] + srcR[s]) * 0.5f * gain * levelGain;
            }
        };

        mixChannel(chanA_phys, gA);
        mixChannel(chanB_phys, gB);
    }
}

int FluidSynthModel::getNumPrograms()
{
    return 128;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int FluidSynthModel::getCurrentProgram()
{
    RangedAudioParameter *param{valueTreeState.getParameter("preset")};
    jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
    AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
    return castParam->get();
}

void FluidSynthModel::setCurrentProgram(int index)
{
    RangedAudioParameter *param{valueTreeState.getParameter("preset")};
    jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
    AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
    // setCurrentProgram() gets invoked from non-message thread.
    // AudioParameterInt#operator= will activate any listeners of audio parameter "preset".
    // This includes TableComponent, who will update its UI.
    // we need to lock the message thread whilst it does that UI update.
    const MessageManagerLock mmLock;
    *castParam = index;
}

const String FluidSynthModel::getProgramName(int index)
{
     fluid_sfont_t* sfont{
         sfont_id == -1
         ? nullptr
         : fluid_synth_get_sfont_by_id(synth.get(), sfont_id)
     };
     if (!sfont) {
         String presetName{"Preset "};
         return presetName << index;
     }
     RangedAudioParameter *param{valueTreeState.getParameter("bank")};
     jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
     AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
     int bank{castParam->get()};
    
     fluid_preset_t *preset{fluid_sfont_get_preset(
         sfont,
         bank,
         index)};
     if (!preset) {
         String presetName{"Preset "};
         return presetName << index;
     }
     return {fluid_preset_get_name(preset)};
}

void FluidSynthModel::changeProgramName(int index, const String& newName)
{
    // no-op; we don't support modifying the soundfont, so let's not support modification of preset names.
}
