// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "Skin.h"
#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace teo5
{
namespace
{
struct SkinDef
{
    juce::String name;
    juce::String description;
    bool light;                       // light drawers (dark text)
    juce::Colour faceTop, faceBottom; // panel face gradient
    juce::Colour tint;                // hue given to dark neutrals (drawers, cheeks, knob bodies)
    float tintAmount;
    juce::Colour text;                // lettering
    float accentHue, accentSat, accentBright; // pinstripes, selections
    float ledHue;                     // LEDs, lit buttons, the bank display
    float ledSat = 1.0f;              // 0 = white LEDs
    float drawerScale = 1.0f;         // custom skins: dark drawer shades scaled to the chosen drawer colour
    juce::String id;                  // custom skins: their identity (saved choice, file)
    Skin::Custom custom;              // custom skins: the colours chosen
};

// The built-in skins, then the user's (custom) ones.
std::vector<SkinDef>& defs()
{
    static std::vector<SkinDef> d = {
        { "Current", "Charcoal, blue pinstripes, red LEDs. As on the TEO-5.", false,
          juce::Colour (0xff2e2f33), juce::Colour (0xff26272a), juce::Colour (0xff2a2b2e), 0.0f, juce::Colour (0xffe6e6e6),
          0.585f, 0.77f, 0.82f, 0.03f },
        { "Studio Silver", "Light silver, graphite lettering, blue LEDs.", true,
          juce::Colour (0xffdfe1e4), juce::Colour (0xffc6c9ce), juce::Colour (0xff8a96a8), 0.10f, juce::Colour (0xff23262b),
          0.59f, 0.78f, 0.74f, 0.58f },
        { "Midnight", "Deep navy, cyan pinstripes, amber LEDs.", false,
          juce::Colour (0xff1b2436), juce::Colour (0xff141b2a), juce::Colour (0xff1c3156), 0.45f, juce::Colour (0xffdfe8f5),
          0.52f, 0.75f, 0.86f, 0.095f },
        { "Walnut", "Warm brown-black, copper lines, orange LEDs.", false,
          juce::Colour (0xff2f2722), juce::Colour (0xff241d19), juce::Colour (0xff4a3020), 0.40f, juce::Colour (0xfff1e6d2),
          0.075f, 0.62f, 0.78f, 0.055f },
        { "Phosphor", "Near-black, green lines and green LEDs.", false,
          juce::Colour (0xff1a1d1b), juce::Colour (0xff121513), juce::Colour (0xff163a24), 0.35f, juce::Colour (0xffd8f0dc),
          0.38f, 0.65f, 0.72f, 0.36f },
        { "Arctic White", "Bright white, black lettering, red LEDs.", true,
          juce::Colour (0xfff5f6f7), juce::Colour (0xffe3e5e8), juce::Colour (0xff9aa3ad), 0.06f, juce::Colour (0xff15171a),
          0.0f, 0.0f, 0.30f, 0.0f },
        { "Oxblood", "Deep burgundy, cream lettering, gold lines.", false,
          juce::Colour (0xff3a1a1e), juce::Colour (0xff2b1216), juce::Colour (0xff5a1a24), 0.45f, juce::Colour (0xfff2e6d8),
          0.12f, 0.60f, 0.85f, 0.10f },
        { "Deep Teal", "Dark teal, aqua lines, ice-blue LEDs.", false,
          juce::Colour (0xff12302f), juce::Colour (0xff0c2423), juce::Colour (0xff0f4a47), 0.45f, juce::Colour (0xffe2f4f2),
          0.47f, 0.60f, 0.85f, 0.50f, 0.55f },
        { "Royal Purple", "Deep violet, lavender lines, magenta LEDs.", false,
          juce::Colour (0xff2a1f3d), juce::Colour (0xff1f162e), juce::Colour (0xff3d2766), 0.45f, juce::Colour (0xffece4fa),
          0.75f, 0.45f, 0.90f, 0.88f },
        { "Graphite", "Monochrome grey, white lines, white LEDs.", false,
          juce::Colour (0xff3a3b3d), juce::Colour (0xff2e2f31), juce::Colour (0xff000000), 0.0f, juce::Colour (0xfff0f0f0),
          0.0f, 0.0f, 0.75f, 0.0f, 0.0f },
    };
    return d;
}

constexpr int builtInSkins = 10;

juce::File customFile()
{
    if (auto f = juce::SystemStats::getEnvironmentVariable ("TEO5_CUSTOM_SKINS_FILE", {}); f.isNotEmpty())
        return juce::File (f);
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/TEO5Editor/custom-skins.json");
}

// A skin made from the six colours chosen in the skin editor.
SkinDef defFor (const Skin::Custom& c, const juce::String& id)
{
    SkinDef d;
    d.name = c.name;
    d.description = "Your skin";
    d.light = c.lightDrawers;
    d.faceTop = c.panel.interpolatedWith (juce::Colours::white, 0.04f);
    d.faceBottom = c.panel.interpolatedWith (juce::Colours::black, 0.12f);
    d.tint = c.drawers;
    d.tintAmount = 0.625f; // the drawers take the chosen colour's strength (see mapFor)
    d.text = c.lettering;
    d.accentHue = c.pinstripes.getHue();
    d.accentSat = c.pinstripes.getSaturation();
    d.accentBright = c.pinstripes.getBrightness();
    d.ledHue = c.leds.getHue();
    d.ledSat = juce::jlimit (0.0f, 1.15f, c.leds.getSaturation() / 0.89f);
    d.drawerScale = juce::jlimit (0.3f, 4.0f, c.drawers.getBrightness() / 0.11f); // 0.11: the design's drawer background
    d.id = id;
    d.custom = c;
    return d;
}

juce::var toVar (const Skin::Custom& c, const juce::String& id)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("name", c.name);
    o->setProperty ("panel", c.panel.toDisplayString (false));
    o->setProperty ("lettering", c.lettering.toDisplayString (false));
    o->setProperty ("pinstripes", c.pinstripes.toDisplayString (false));
    o->setProperty ("leds", c.leds.toDisplayString (false));
    o->setProperty ("drawers", c.drawers.toDisplayString (false));
    o->setProperty ("lightDrawers", c.lightDrawers);
    return juce::var (o);
}

std::optional<Skin::Custom> fromVar (const juce::var& v)
{
    auto colour = [&v] (const char* key) { return juce::Colour::fromString ("ff" + v.getProperty (key, "000000").toString()); };
    if (! v.isObject() || v.getProperty ("name", {}).toString().isEmpty())
        return std::nullopt;
    return Skin::Custom { v.getProperty ("name", {}).toString(), colour ("panel"), colour ("lettering"), colour ("pinstripes"),
                          colour ("leds"), colour ("drawers"), (bool) v.getProperty ("lightDrawers", false) };
}

void loadCustom()
{
    static bool loaded = false;
    if (std::exchange (loaded, true))
        return;
    const auto json = juce::JSON::parse (customFile()); // kept: the array below lives inside it
    if (auto* arr = json.getArray())
        for (auto& v : *arr)
            if (auto c = fromVar (v))
                defs().push_back (defFor (*c, v.getProperty ("id", juce::Uuid().toString()).toString()));
}

void saveCustom()
{
    juce::Array<juce::var> arr;
    for (int i = builtInSkins; i < (int) defs().size(); ++i)
        arr.add (toVar (defs()[(size_t) i].custom, defs()[(size_t) i].id));
    if (customFile().getParentDirectory().createDirectory())
        customFile().replaceWithText (juce::JSON::toString (juce::var (arr)));
}

juce::File skinFile()
{
    if (auto f = juce::SystemStats::getEnvironmentVariable ("TEO5_SKIN_FILE", {}); f.isNotEmpty())
        return juce::File (f); // tools and tests: never the user's choice
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/TEO5Editor/skin.txt");
}

// The saved choice: a built-in skin's number, or "custom:<id>" (a number would point elsewhere once one is deleted).
int indexFor (const juce::String& saved)
{
    loadCustom();
    if (saved.startsWith ("custom:"))
    {
        for (int i = builtInSkins; i < (int) defs().size(); ++i)
            if (defs()[(size_t) i].id == saved.fromFirstOccurrenceOf (":", false, false))
                return i;
        return 0;
    }
    return juce::jlimit (0, builtInSkins - 1, saved.getIntValue());
}

juce::String savedFormOf (int index)
{
    return index >= builtInSkins ? "custom:" + defs()[(size_t) index].id : juce::String (index);
}

// The skin in use; -1 until Skin::initialise() (nothing is read from disk while the plug-in's code is loading, so
// tools and tests can point the files elsewhere first). Until then the editor's own colours are used.
int& currentIndex()
{
    static int index = -1;
    return index;
}

int inUse() { return juce::jmax (0, currentIndex()); }

// What each colour shown on screen was designed as, per skin, so colours already set on components can be re-mapped.
struct Origin { juce::uint32 argb; Skin::Area area; };
std::map<std::pair<int, juce::uint32>, Origin>& origins()
{
    static std::map<std::pair<int, juce::uint32>, Origin> m;
    return m;
}

std::vector<SkinColour*>& skinColours()
{
    static std::vector<SkinColour*> v;
    return v;
}

struct LookAndFeelEntry { juce::LookAndFeel_V4* lnf; std::function<void()> after; };
std::vector<LookAndFeelEntry>& lookAndFeels()
{
    static std::vector<LookAndFeelEntry> v;
    return v;
}

juce::Colour mapFor (const SkinDef& s, int index, juce::uint32 argb, Skin::Area area)
{
    const juce::Colour c (argb);
    if (index == 0)
        return c; // the design itself
    const float a = c.getFloatAlpha();
    if (area == Skin::Area::Text)
        return s.text.withAlpha (a);
    float h, sat, b;
    c.getHSB (h, sat, b);

    if (sat < 0.2f || b < 0.09f) // neutral (very dark reds count too: bezel shadows, the display's background)
    {
        if (sat >= 0.2f && (h < 0.08f || h > 0.92f)) // the display's dark red background follows the LED colour
            return juce::Colour::fromHSV (s.ledHue, sat * s.ledSat, b, a);
        if (area == Skin::Area::Face)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (b - 0.07f) / 0.14f);
            return s.faceBottom.interpolatedWith (s.faceTop, t).withAlpha (a);
        }
        if (area == Skin::Area::Drawer && s.light)
        {
            // Light drawers: dark backgrounds become light, light text dark; nearly opaque ones become opaque (the
            // panel showing through a light drawer looks like ghosting).
            const float nb = juce::jlimit (0.0f, 1.0f, 1.0f - b * 0.82f);
            const float a2 = a > 0.9f ? 1.0f : a;
            return juce::Colour::fromHSV (0.0f, 0.0f, nb, a2).interpolatedWith (s.tint.withAlpha (a2), s.tintAmount * (1.0f - nb) + 0.04f);
        }
        if (b > 0.75f || a < 0.6f)
            return c; // whites and translucent shading stay
        const float amount = area == Skin::Area::Drawer ? s.tintAmount : s.tintAmount * 0.35f;
        const float nb = area == Skin::Area::Drawer ? juce::jmin (0.92f, b * s.drawerScale) : b;
        return juce::Colour::fromHSV (s.tint.getHue(), juce::jmin (1.0f, s.tint.getSaturation() * amount * 1.6f), nb, a);
    }
    if (h < 0.075f || h > 0.92f) // red/orange: LEDs, lit buttons, segments, red text
        return juce::Colour::fromHSV (s.ledHue, sat * s.ledSat, b, a);
    if (s.ledSat < 0.5f && h < 0.12f && sat < 0.4f && b > 0.9f) // the warm glint in a lit LED: white on white-LED skins
        return juce::Colour::fromHSV (s.ledHue, sat * s.ledSat, b, a);
    if (h > 0.5f && h < 0.7f) // blue: pinstripes, selections, tabs
        return juce::Colour::fromHSV (s.accentHue, juce::jmin (1.0f, sat * (s.accentSat / 0.77f)),
                                      juce::jmin (1.0f, b * (s.accentBright / 0.82f)), a);
    return c; // ambers, greens etc. keep their meaning
}

// LookAndFeel_V4's own colours (drop-downs, text fields, menus, scroll bars) follow the drawer colours.
juce::LookAndFeel_V4::ColourScheme schemeFor (int index)
{
    using Scheme = juce::LookAndFeel_V4::ColourScheme;
    auto scheme = juce::LookAndFeel_V4::getDarkColourScheme();
    if (index == 0)
        return scheme;
    for (int i = 0; i < Scheme::numColours; ++i)
    {
        const auto id = (Scheme::UIColour) i;
        auto c = scheme.getUIColour (id);
        // JUCE's dark scheme is a slightly blue grey: its backgrounds, outlines and text are shades, not accents.
        if (id != Scheme::defaultFill && id != Scheme::highlightedFill)
            c = juce::Colour::greyLevel (c.getBrightness()).withAlpha (c.getAlpha());
        scheme.setUIColour (id, mapFor (defs()[(size_t) index], index, c.getARGB(), Skin::Area::Drawer));
    }
    return scheme;
}

juce::ChangeBroadcaster& broadcaster()
{
    static juce::ChangeBroadcaster b;
    return b;
}
} // namespace

int Skin::count() { loadCustom(); return (int) defs().size(); }
int Skin::builtInCount() { return builtInSkins; }
bool Skin::isCustom (int index) { return index >= builtInSkins && index < count(); }
Skin::Info Skin::info (int index)
{
    const auto& d = defs()[(size_t) juce::jlimit (0, count() - 1, index)];
    return { d.name, d.description };
}

// The six colours of any skin (to start a custom skin from the one in use).
Skin::Custom Skin::coloursOf (int index)
{
    index = juce::jlimit (0, count() - 1, index);
    const auto& d = defs()[(size_t) index];
    if (isCustom (index))
        return d.custom;
    return { d.name + " copy", d.faceTop.interpolatedWith (d.faceBottom, 0.5f), mapFor (d, index, 0xffe6e6e6, Area::Text),
             mapFor (d, index, 0xff2f7fd0, Area::Panel), mapFor (d, index, 0xffff4a1c, Area::Panel),
             mapFor (d, index, 0xff181a1c, Area::Drawer), d.light };
}

double Skin::contrast (juce::Colour a, juce::Colour b)
{
    auto lum = [] (juce::Colour c)
    {
        auto ch = [] (juce::uint8 v) { const double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow ((x + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
    };
    const double x = lum (a), y = lum (b);
    return (juce::jmax (x, y) + 0.05) / (juce::jmin (x, y) + 0.05);
}

int Skin::addCustom (const Custom& c, bool save)
{
    loadCustom();
    defs().push_back (defFor (c, juce::Uuid().toString()));
    if (save)
        saveCustom();
    return count() - 1;
}

void Skin::updateCustom (int index, const Custom& c, bool save)
{
    if (! isCustom (index))
        return;
    defs()[(size_t) index] = defFor (c, defs()[(size_t) index].id);
    if (save)
        saveCustom();
    if (index == inUse())
        reapply (true); // the skin in use changed: re-colour everything
}

void Skin::removeCustom (int index)
{
    if (! isCustom (index))
        return;
    const auto wasCurrent = inUse();
    defs().erase (defs().begin() + index);
    saveCustom();
    // Indices after it moved down: forget their recorded colours (the registry is keyed by index).
    for (auto it = origins().begin(); it != origins().end();)
        it = it->first.first >= index ? origins().erase (it) : std::next (it);
    if (wasCurrent == index)
        select (0);
    else if (wasCurrent > index)
        currentIndex() = wasCurrent - 1;
}

bool Skin::exportCustom (int index, const juce::File& file)
{
    return isCustom (index) && file.replaceWithText (juce::JSON::toString (toVar (defs()[(size_t) index].custom, {})));
}

std::optional<Skin::Custom> Skin::readCustom (const juce::File& file)
{
    return fromVar (juce::JSON::parse (file));
}
int Skin::current() { return inUse(); }

void Skin::initialise()
{
    if (currentIndex() >= 0)
        return;
    auto v = juce::SystemStats::getEnvironmentVariable ("TEO5_SKIN", {});
    if (v.isEmpty())
        v = skinFile().loadFileAsString().trim();
    currentIndex() = indexFor (v);
    reapply (false);
}
juce::ChangeBroadcaster& Skin::changes() { return broadcaster(); }

juce::Colour Skin::map (juce::uint32 argb, Area area)
{
    const int index = inUse();
    const auto result = mapFor (defs()[(size_t) index], index, argb, area);
    origins().emplace (std::make_pair (index, result.getARGB() | 0xff000000u), Origin { argb | 0xff000000u, area });
    return result;
}

void Skin::select (int index, bool temporary)
{
    initialise();
    index = juce::jlimit (0, count() - 1, index);
    if (index == currentIndex())
        return;
    currentIndex() = index;
    reapply (! temporary);
    if (! temporary && skinFile().getParentDirectory().createDirectory())
        skinFile().replaceWithText (savedFormOf (index));
}

void Skin::rememberChoice()
{
    if (skinFile().getParentDirectory().createDirectory())
        skinFile().replaceWithText (savedFormOf (inUse()));
}

// The current skin (or its colours) changed: named colours, look-and-feels, then (notify) every open editor.
void Skin::reapply (bool notify)
{
    const int index = inUse();
    for (auto* c : skinColours())
        c->refresh();
    for (auto& e : lookAndFeels())
    {
        e.lnf->setColourScheme (schemeFor (index));
        if (e.after)
            e.after();
    }
    if (notify)
        broadcaster().sendSynchronousChangeMessage();
}

void Skin::registerLookAndFeel (juce::LookAndFeel_V4& lnf, std::function<void()> afterChange)
{
    lookAndFeels().push_back ({ &lnf, afterChange });
    lnf.setColourScheme (schemeFor (inUse()));
    if (afterChange)
        afterChange();
}

void Skin::unregisterLookAndFeel (juce::LookAndFeel_V4& lnf)
{
    auto& v = lookAndFeels();
    v.erase (std::remove_if (v.begin(), v.end(), [&lnf] (const LookAndFeelEntry& e) { return e.lnf == &lnf; }), v.end());
}

void Skin::recolourComponents (juce::Component& root, int fromSkin)
{
    const int to = inUse();
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        auto& props = c.getProperties();
        std::vector<std::pair<int, juce::Colour>> changes;
        for (int i = 0; i < props.size(); ++i)
        {
            const auto name = props.getName (i).toString();
            if (! name.startsWith ("jcclr_"))
                continue;
            const auto shown = juce::Colour ((juce::uint32) (int) props.getValueAt (i));
            auto it = origins().find ({ fromSkin, shown.getARGB() | 0xff000000u });
            if (it == origins().end())
                continue;
            const auto mapped = mapFor (defs()[(size_t) to], to, it->second.argb, it->second.area);
            origins().insert_or_assign (std::make_pair (to, mapped.getARGB() | 0xff000000u), it->second);
            changes.push_back ({ name.substring (6).getHexValue32(), mapped.withAlpha (shown.getFloatAlpha()) });
        }
        for (auto& [id, colour] : changes)
            c.setColour (id, colour);
        for (auto* child : c.getChildren())
            walk (*child);
    };
    walk (root);
    root.repaint();
}

SkinColour::SkinColour (juce::uint32 argb, Skin::Area a) : colour (Skin::map (argb, a)), original (argb), area (a)
{
    skinColours().push_back (this);
}

SkinColour::SkinColour (const SkinColour& other) : colour (other.colour), original (other.original), area (other.area)
{
    skinColours().push_back (this);
}

SkinColour::~SkinColour()
{
    auto& v = skinColours();
    v.erase (std::remove (v.begin(), v.end(), this), v.end());
}

void SkinColour::refresh()
{
    colour = Skin::map (original, area);
}

} // namespace teo5
