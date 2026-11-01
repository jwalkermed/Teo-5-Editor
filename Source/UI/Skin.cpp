// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "Skin.h"
#include <map>
#include <vector>

namespace teo5
{
namespace
{
struct SkinDef
{
    const char* name;
    const char* description;
    bool light;                       // light drawers (dark text)
    juce::Colour faceTop, faceBottom; // panel face gradient
    juce::Colour tint;                // hue given to dark neutrals (drawers, cheeks, knob bodies)
    float tintAmount;
    juce::Colour text;                // lettering
    float accentHue, accentSat, accentBright; // pinstripes, selections
    float ledHue;                     // LEDs, lit buttons, the bank display
    float ledSat = 1.0f;              // 0 = white LEDs
};

const std::vector<SkinDef>& defs()
{
    static const std::vector<SkinDef> d = {
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

juce::File skinFile()
{
    if (auto f = juce::SystemStats::getEnvironmentVariable ("TEO5_SKIN_FILE", {}); f.isNotEmpty())
        return juce::File (f); // tools and tests: never the user's choice
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/TEO5Editor/skin.txt");
}

int& currentIndex()
{
    static int index = []
    {
        auto v = juce::SystemStats::getEnvironmentVariable ("TEO5_SKIN", {});
        if (v.isEmpty())
            v = skinFile().loadFileAsString().trim();
        return juce::jlimit (0, (int) defs().size() - 1, v.getIntValue());
    }();
    return index;
}

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
        return juce::Colour::fromHSV (s.tint.getHue(), s.tint.getSaturation() * amount * 1.6f, b, a);
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

int Skin::count() { return (int) defs().size(); }
Skin::Info Skin::info (int index)
{
    const auto& d = defs()[(size_t) juce::jlimit (0, count() - 1, index)];
    return { d.name, d.description };
}
int Skin::current() { return currentIndex(); }
juce::ChangeBroadcaster& Skin::changes() { return broadcaster(); }

juce::Colour Skin::map (juce::uint32 argb, Area area)
{
    const int index = currentIndex();
    const auto result = mapFor (defs()[(size_t) index], index, argb, area);
    origins().emplace (std::make_pair (index, result.getARGB() | 0xff000000u), Origin { argb | 0xff000000u, area });
    return result;
}

void Skin::select (int index, bool temporary)
{
    index = juce::jlimit (0, count() - 1, index);
    if (index == currentIndex())
        return;
    currentIndex() = index;
    for (auto* c : skinColours())
        c->refresh();
    for (auto& e : lookAndFeels())
    {
        e.lnf->setColourScheme (schemeFor (index));
        if (e.after)
            e.after();
    }
    if (! temporary)
    {
        if (skinFile().getParentDirectory().createDirectory())
            skinFile().replaceWithText (juce::String (index));
        broadcaster().sendSynchronousChangeMessage();
    }
}

void Skin::registerLookAndFeel (juce::LookAndFeel_V4& lnf, std::function<void()> afterChange)
{
    lookAndFeels().push_back ({ &lnf, afterChange });
    lnf.setColourScheme (schemeFor (currentIndex()));
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
    const int to = currentIndex();
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
            origins().emplace (std::make_pair (to, mapped.getARGB() | 0xff000000u), it->second);
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
