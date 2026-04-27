//
// Created by Alex Birch on 17/09/2017.
//

#include "TablesComponent.h"

using namespace std;
using namespace placeholders;

TablesComponent::TablesComponent(
    AudioProcessorValueTreeState& valueTreeState
)
: valueTreeState{valueTreeState}
, banks{valueTreeState}
, presetTable{valueTreeState}
{

    presetTable.setWantsKeyboardFocus(false);

    searchBox.setTextToShowWhenEmpty("Search presets\xe2\x80\xa6", juce::Colours::grey);
    searchBox.setWantsKeyboardFocus(true);
    searchBox.onTextChange = [this]() {
        presetTable.setSearchQuery(searchBox.getText());
    };

    addAndMakeVisible(presetTable);
    addAndMakeVisible(banks);
    addAndMakeVisible(searchBox);
}

void TablesComponent::resized() {
    Rectangle<int> r (getLocalBounds());
    banks.setBounds (r.removeFromTop(27).reduced(5,0));
    searchBox.setBounds (r.removeFromTop(24).reduced(5, 2));

    presetTable.setBounds (r);
}

bool TablesComponent::keyPressed(const KeyPress &key) {
    if (key.getKeyCode() == KeyPress::leftKey
            || key.getKeyCode() == KeyPress::rightKey) {
        banks.cycle(key.getKeyCode() == KeyPress::rightKey);
        return true;
    }
    return presetTable.keyPressed(key);
}
