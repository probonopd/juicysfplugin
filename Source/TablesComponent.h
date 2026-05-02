//
// Created by Alex Birch on 17/09/2017.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "Pills.h"
#include "TableComponent.h"
#include "FluidSynthModel.h"
#include <memory>
#include <fluidsynth.h>

using namespace std;

class TablesComponent : public Component,
                        public Button::Listener,
                        public ValueTree::Listener
{
public:
    TablesComponent(
        AudioProcessorValueTreeState& valueTreeState
    );
    ~TablesComponent();

    void resized() override;

    bool keyPressed(const KeyPress &key) override;

    // Button::Listener
    void buttonClicked(Button* button) override;

    // ValueTree::Listener (to keep step button toggle states in sync)
    void valueTreePropertyChanged(ValueTree& tree, const Identifier& property) override;
    void valueTreeChildAdded(ValueTree&, ValueTree&) override {}
    void valueTreeChildRemoved(ValueTree&, ValueTree&, int) override {}
    void valueTreeChildOrderChanged(ValueTree&, int, int) override {}
    void valueTreeParentChanged(ValueTree&) override {}
    void valueTreeRedirected(ValueTree&) override {}

private:
    void updateStepButtonStates();

    AudioProcessorValueTreeState& valueTreeState;

    // Wave-seq step selector buttons (A/B/C/D)
    TextButton stepButtons[4];

    Pills banks;
    TableComponent presetTable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TablesComponent)
};
