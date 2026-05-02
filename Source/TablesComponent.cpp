//
// Created by Alex Birch on 17/09/2017.
//

#include "TablesComponent.h"

using namespace std;
using namespace placeholders;

static const char* kStepNames[] = {"A", "B", "C", "D"};

TablesComponent::TablesComponent(
    AudioProcessorValueTreeState& valueTreeState
)
: valueTreeState{valueTreeState}
, banks{valueTreeState}
, presetTable{valueTreeState}
{
    presetTable.setWantsKeyboardFocus(false);
    addAndMakeVisible(presetTable);
    addAndMakeVisible(banks);

    // Set up A/B/C/D step selector buttons
    for (int i = 0; i < 4; ++i) {
        stepButtons[i].setButtonText(kStepNames[i]);
        stepButtons[i].setRadioGroupId(77777);
        stepButtons[i].setClickingTogglesState(true);
        stepButtons[i].addListener(this);
        addAndMakeVisible(stepButtons[i]);
    }

    // Initialise toggle state from current activeBrowserStep
    updateStepButtonStates();

    valueTreeState.state.addListener(this);
}

TablesComponent::~TablesComponent() {
    valueTreeState.state.removeListener(this);
    for (auto& btn : stepButtons)
        btn.removeListener(this);
}

void TablesComponent::buttonClicked(Button* button) {
    for (int i = 0; i < 4; ++i) {
        if (button == &stepButtons[i]) {
            // Store the active step in the value tree state — TableComponent will
            // respond via its valueTreePropertyChanged and navigate to that step's preset.
            valueTreeState.state.setProperty("activeBrowserStep", i, nullptr);
            return;
        }
    }
}

void TablesComponent::updateStepButtonStates() {
    const int active = static_cast<int>(
        valueTreeState.state.getProperty("activeBrowserStep", 0));
    for (int i = 0; i < 4; ++i)
        stepButtons[i].setToggleState(i == active, dontSendNotification);
}

void TablesComponent::valueTreePropertyChanged(ValueTree& tree, const Identifier& property) {
    if (property == Identifier("activeBrowserStep"))
        updateStepButtonStates();
}

void TablesComponent::resized() {
    Rectangle<int> r (getLocalBounds());

    // Row 1: step selector buttons (A B C D), same height as bank pills
    Rectangle<int> stepRow = r.removeFromTop(27).reduced(5, 0);
    const int btnW = stepRow.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        stepButtons[i].setBounds(stepRow.removeFromLeft(btnW));

    // Row 2: bank pills
    banks.setBounds(r.removeFromTop(27).reduced(5, 0));

    presetTable.setBounds(r);
}

bool TablesComponent::keyPressed(const KeyPress &key) {
    if (key.getKeyCode() == KeyPress::leftKey
            || key.getKeyCode() == KeyPress::rightKey) {
        banks.cycle(key.getKeyCode() == KeyPress::rightKey);
        return true;
    }
    return presetTable.keyPressed(key);
}
