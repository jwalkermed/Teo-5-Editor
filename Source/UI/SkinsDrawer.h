// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Skin.h"

namespace teo5
{

// Right-side drawer listing the skins, each with a small picture of the front panel in it; a click switches the
// whole editor at once. The choice is saved for every editor on this Mac (see Skin.h). The list scrolls when the
// editor is too small to show it all. Custom skins (MY SKINS) are made and changed in the drawer's skin editor:
// six colours, shown live on the whole editor while you pick them.
class SkinsDrawer : public juce::Component
{
public:
    SkinsDrawer();
    ~SkinsDrawer() override;

    // Draws the front panel in skin `index` (the editor provides it; the pictures are made when the drawer opens).
    std::function<juce::Image (int index)> renderPanel;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // Skin editor: a new skin starting from the one in use, or an existing custom skin.
    void startNew();
    void startEdit (int index);
    bool isEditing() const;
    void cancelEdit();
    void showSkin (int index); // scroll the list to a skin (after Save, Duplicate or Import)

private:
    class List;
    class Editor;
    void refreshThumbnails();
    void duplicate (int index);
    void confirmDelete (int index);
    void exportSkin (int index);
    void importSkin();

    std::unique_ptr<List> list;
    std::unique_ptr<Editor> editor;
    juce::Viewport viewport;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace teo5
