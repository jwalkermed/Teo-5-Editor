// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "ProgramLayout.h"
#include "../Params/ParamTable.h"
#include <set>
#include <string>

namespace teo5
{

juce::File ProgramLayout::defaultFile()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("TEO5_LAYOUT_FILE", {}); env.isNotEmpty())
        return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("TEO5Editor").getChildFile ("program-layout.json");
}

const ProgramLayout::Field* ProgramLayout::field (const juce::String& paramId) const
{
    auto it = fields.find (paramId);
    return it != fields.end() ? &it->second : nullptr;
}

int ProgramLayout::read (const juce::MemoryBlock& program, const Field& f) const
{
    if (f.offset < 0 || f.offset + f.width > (int) program.getSize())
        return 0;
    auto* d = static_cast<const juce::uint8*> (program.getData()) + f.offset;
    int v = 0;
    for (int i = 0; i < f.width; ++i)
    {
        const int byte = f.littleEndian ? d[i] : d[f.width - 1 - i];
        v |= byte << (8 * i);
    }
    return v;
}

void ProgramLayout::write (juce::MemoryBlock& program, const Field& f, int value) const
{
    if (f.offset < 0 || f.offset + f.width > (int) program.getSize())
        return;
    auto* d = static_cast<juce::uint8*> (program.getData()) + f.offset;
    for (int i = 0; i < f.width; ++i)
    {
        const auto byte = (juce::uint8) ((value >> (8 * i)) & 0xff);
        (f.littleEndian ? d[i] : d[f.width - 1 - i]) = byte;
    }
}

juce::String ProgramLayout::readName (const juce::MemoryBlock& program) const
{
    if (nameOffset < 0 || nameOffset + nameLength > (int) program.getSize())
        return {};
    auto* d = static_cast<const char*> (program.getData()) + nameOffset;
    juce::String s;
    for (int i = 0; i < nameLength && d[i] != 0; ++i)
        s += (juce::juce_wchar) (juce::uint8) d[i];
    return s.trimEnd();
}

void ProgramLayout::writeName (juce::MemoryBlock& program, const juce::String& name) const
{
    if (nameOffset < 0 || nameOffset + nameLength > (int) program.getSize())
        return;
    auto* d = static_cast<char*> (program.getData()) + nameOffset;
    for (int i = 0; i < nameLength; ++i)
    {
        const auto c = i < name.length() ? name[i] : ' ';
        d[i] = (char) (c >= 32 && c < 127 ? c : ' ');
    }
}

ProgramLayout ProgramLayout::builtIn()
{
    ProgramLayout l;
    for (auto& def : allParams())
        if (def.scope == Scope::Program && def.nrpn >= 0 && def.nrpn < 4096 && def.maxValue > def.minValue)
            l.fields[def.id] = { def.nrpn, def.maxValue > 255 ? 2 : 1, true };
    l.nameOffset = 159;
    l.nameLength = 20;
    l.nameVersion = currentNameVersion;
    return l;
}

std::optional<ProgramLayout::Field> ProgramLayout::infer (const std::vector<Sample>& samples, int preferredOffset)
{
    if (samples.size() < 2)
        return std::nullopt;
    const auto size = samples.front().first.getSize();
    for (auto& s : samples)
        if (s.first.getSize() != size)
            return std::nullopt;

    // Positions that differ between any two samples.
    auto* base = static_cast<const juce::uint8*> (samples.front().first.getData());
    juce::Array<int> changed;
    for (int i = 0; i < (int) size; ++i)
        for (size_t s = 1; s < samples.size(); ++s)
            if (static_cast<const juce::uint8*> (samples[s].first.getData())[i] != base[i])
            {
                changed.add (i);
                break;
            }

    // Accept the first 1- or 2-byte interpretation that decodes every sample to its value, trying the
    // preferred offset first (switches that interlock change other bytes in step with the probed one).
    ProgramLayout probe;
    if (changed.contains (preferredOffset))
    {
        changed.removeFirstMatchingValue (preferredOffset);
        changed.insert (0, preferredOffset);
    }
    for (int start : changed)
        for (int width : { 1, 2 })
            for (bool le : { true, false })
                for (int off : { start, start - 1 })
                {
                    if (off < 0 || (width == 1 && off != start))
                        continue;
                    const Field f { off, width, le };
                    bool all = true;
                    for (auto& [dump, value] : samples)
                        all = all && probe.read (dump, f) == value;
                    if (all)
                        return f;
                }
    return std::nullopt;
}

std::optional<std::pair<int, int>> ProgramLayout::detectNameField (const std::vector<juce::MemoryBlock>& programs)
{
    if (programs.size() < 4)
        return std::nullopt;
    const int size = (int) programs.front().getSize();
    for (auto& p : programs)
        if ((int) p.getSize() != size)
            return std::nullopt;

    constexpr int maxLen = 20;
    auto printable = [] (juce::uint8 b) { return b >= 0x20 && b < 0x7f; };
    auto isLetter = [] (char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); };

    // A name looks like words: mostly letters, no long runs of one character. Other data that happens to be
    // printable (e.g. sequencer steps full of note 60 = '<') fails this.
    struct Candidate { bool wordLike; int letters; std::string text; int run; };
    auto candidateAt = [&] (const juce::uint8* d, int o) -> std::optional<Candidate>
    {
        if (! printable (d[o]) || d[o] == ' ')
            return std::nullopt;
        int run = 0;
        while (run < maxLen && o + run < size && printable (d[o + run]))
            ++run;
        std::string text (reinterpret_cast<const char*> (d + o), (size_t) run);
        while (! text.empty() && text.back() == ' ')
            text.pop_back();

        int letters = 0, nonSpace = 0, same = 1, maxSame = 1;
        for (size_t i = 0; i < text.size(); ++i)
        {
            letters += isLetter (text[i]) ? 1 : 0;
            nonSpace += text[i] != ' ' ? 1 : 0;
            same = (i > 0 && text[i] == text[i - 1]) ? same + 1 : 1;
            maxSame = juce::jmax (maxSame, same);
        }
        const bool wordLike = text.size() >= 2 && maxSame <= 3 && letters * 2 >= nonSpace;
        return Candidate { wordLike, letters, text, run };
    };

    int bestOffset = -1, bestLength = 0;
    double bestScore = 0.0;
    for (int o = 0; o < size - 3; ++o)
    {
        int good = 0, letters = 0, longest = 0;
        bool ok = true;
        std::set<std::string> distinct;
        for (auto& p : programs)
        {
            auto c = candidateAt (static_cast<const juce::uint8*> (p.getData()), o);
            if (! c)
            {
                ok = false;
                break;
            }
            good += c->wordLike ? 1 : 0;
            letters += c->letters;
            longest = juce::jmax (longest, c->run);
            distinct.insert (c->text);
        }
        if (! ok)
            continue;

        const double n = (double) programs.size();
        const double goodFraction = good / n, variety = (double) distinct.size() / n, avgLetters = letters / n;
        if (goodFraction < 0.75 || variety < 0.6 || avgLetters < 3.0)
            continue; // a few names may be numbers ("2049"), but most must look like words
        const double score = goodFraction * avgLetters * variety;
        if (score > bestScore)
        {
            bestScore = score;
            bestOffset = o;
            bestLength = longest;
        }
    }
    if (bestOffset < 0)
        return std::nullopt;
    return std::make_pair (bestOffset, bestLength);
}

bool ProgramLayout::load (const juce::File& file)
{
    auto json = juce::JSON::parse (file);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
        return false;

    fields.clear();
    fieldsVersion = obj->hasProperty ("fieldsVersion") ? (int) obj->getProperty ("fieldsVersion") : 0;
    nameOffset = obj->getProperty ("nameOffset");
    nameLength = obj->hasProperty ("nameLength") ? (int) obj->getProperty ("nameLength") : 20;
    nameVersion = obj->hasProperty ("nameVersion") ? (int) obj->getProperty ("nameVersion") : 0;
    if (auto* f = obj->getProperty ("fields").getDynamicObject())
        for (auto& p : f->getProperties())
            if (auto* e = p.value.getDynamicObject())
                fields[p.name.toString()] = { (int) e->getProperty ("offset"), (int) e->getProperty ("width"),
                                              (bool) e->getProperty ("littleEndian") };
    return true;
}

bool ProgramLayout::save (const juce::File& file) const
{
    auto* f = new juce::DynamicObject();
    for (auto& [id, fld] : fields)
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("offset", fld.offset);
        e->setProperty ("width", fld.width);
        e->setProperty ("littleEndian", fld.littleEndian);
        f->setProperty (id, juce::var (e));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("nameOffset", nameOffset);
    root->setProperty ("nameLength", nameLength);
    root->setProperty ("nameVersion", nameVersion);
    root->setProperty ("fieldsVersion", fieldsVersion);
    root->setProperty ("fields", juce::var (f));

    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

} // namespace teo5
