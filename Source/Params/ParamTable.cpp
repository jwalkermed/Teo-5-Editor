#include "ParamTable.h"

namespace teo5
{

namespace
{
constexpr auto C = ParamKind::Continuous;
constexpr auto B = ParamKind::Bipolar;
constexpr auto S = ParamKind::Switch;
constexpr auto L = ParamKind::Choice;

constexpr auto P = Scope::Program;
constexpr auto G = Scope::Global;
constexpr auto X = Scope::Control;
constexpr auto U = Scope::Panel;

constexpr const char* onOff = "Off|On";
constexpr const char* lfoShapes = "Triangle|Saw|Reverse Saw|Square|S/H";
// 12 types are documented but the range is 0-12 (13 values). TODO: verify order and the 13th entry.
constexpr const char* fxTypes = "Delay|BBD|Tape Delay 1|Tape Delay 2|Chorus|Flanger|Phaser|HP Filter|Distortion|Ring Mod|Rotating Speaker|Lo-Fi|Type 13";
constexpr const char* clockDivs = "1/2|1/4|1/8|1/8T|1/16|1/16T|1/32|1/64";
constexpr const char* arpModes = "Up|Down|Up+Down|Random|Assign";
constexpr const char* arpRanges = "1 Oct|2 Oct|3 Oct";
constexpr const char* arpRepeats = "Off|1|2|3";
constexpr const char* envRepeats = "Off|ENV1|ENV2|ENV1+ENV2";
constexpr const char* envRoutes = "Filter / Amp|Aux / Filter+Amp|Aux / Filter+Gate"; // TODO: verify order
constexpr const char* glideModes = "Fixed Rate|Fixed Rate A|Fixed Time|Fixed Time A";  // confirmed by the user's guide
constexpr const char* keyModes = "Low|High|Last";                                      // order as in the user's guide
constexpr const char* transposes = "-2 Oct|-1 Oct|0|+1 Oct|+2 Oct";                    // TODO: verify
// The 65 built-in tunings ("Appendix F: Alternative Tunings" in the user's guide), shortened.
const char* const tuningNames[] = {
    "12-Tone Equal Temperament",
    "Harmonic Series",
    "Carlos Harmonic Twelve Tone",
    "Meantone Temperament",
    "1/4 Tone Equal Temperament",
    "19 Tone Equal Temperament",
    "31 Tone Equal Temperament",
    "Pythagorean C",
    "Just Intonation in A, 7-Limit Tritone",
    "3-5 Lattice in A",
    "3-7 Lattice in A",
    "Other Music 7-Limit Black Keys",
    "Dan Schmidt Pelog/Slendro",
    "Yamaha Just Major C",
    "Yamaha Just Minor C",
    "Partch 43-Note Just Intonation",
    "Arabic 12-Tone",
    "12 of 19-tET (Mandelbaum)",
    "12 of 31-tET, Meantone Eb-G#",
    "Riley Harp of New Albion",
    "Artusi Lute Tuning",
    "Bach Well Temperament (Breetvelt)",
    "Bulgarian Bagpipe",
    "Wendy Carlos Alpha",
    "Wendy Carlos Beta",
    "Wendy Carlos Gamma",
    "Carlos Super Just",
    "Catler 24-Tone JI",
    "Chalmers JI-1",
    "Chalmers JI-3",
    "Chalmers JI-4",
    "Chinese, 4th Century",
    "Chinese Lu (Huai Nan Zi)",
    "Colonna 1",
    "Colonna 2",
    "Darreg Megalyra 19 Ratios",
    "Dorian Diatonic Tonos",
    "Almost Equal 12 of Duodenarium",
    "Ellis Just Harmonium",
    "Bali/Java Slendro, Siam 7",
    "Tibetan Ceremonial",
    "Erlangen, Revised",
    "Euler",
    "Fokker 7-Limit 12-Tone Just",
    "Fortuna Bagpipe",
    "Gamelan Udan Mas",
    "Kraig Grady Centaur",
    "Harmonics 1-12 and Subharmonics",
    "Michael Harrison Revelation",
    "Helmholtz Harmonium",
    "North Indian Gamut",
    "Carnatic Gamut",
    "South Indian Vina (Ellis)",
    "7-Limit 12-Tone Scale",
    "Alternate 7-Limit 12-Tone",
    "Kurzweil Just with Natural b7",
    "3 and 7 Prime 17-tET",
    "Johnston 11-Limit Prime Row",
    "1/9 Harrison Comma Meantone",
    "Rousseau Monochord",
    "Persian Santur",
    "Vallotti & Young",
    "LaMonte Young Guitar '58",
    "LaMonte Young Well-Tuned Piano",
    "Thomas Young Well Temperament",
};

// "1. 12-Tone Equal Temperament|2. Harmonic Series|...", optionally after a first entry such as "Global".
const char* tuningChoices (bool withGlobal)
{
    static juce::String global, program;
    auto& s = withGlobal ? program : global;
    if (s.isEmpty())
    {
        juce::StringArray items;
        if (withGlobal)
            items.add ("Global");
        for (int i = 0; i < (int) std::size (tuningNames); ++i)
            items.add (juce::String (i + 1) + ". " + tuningNames[i]);
        s = items.joinIntoString ("|");
    }
    return s.toRawUTF8();
}

constexpr const char* unisonVoiceNames = "1|2|3|4|5|6";                               // TODO: verify meaning of 0-5

juce::Array<ParamDef> buildTable()
{
    juce::Array<ParamDef> t;
    auto add = [&t] (const char* id, const char* name, int nrpn, int cc, int lo, int hi, int def,
                     ParamKind kind, Scope scope, const char* choices = nullptr, bool verify = false)
    {
        t.add ({ id, name, nrpn, cc, lo, hi, def, kind, scope, choices, verify });
    };

    // ---- Oscillators ------------------------------------------------------------------------
    add ("osc1Freq",   "Osc 1 Freq",    1,   8, 0, 63, 24, C, P, nullptr, true);
    add ("osc2Freq",   "Osc 2 Freq",    2,  13, 0, 63, 24, C, P, nullptr, true);
    add ("osc1Detune", "Osc 1 Detune",  3,  -1, 0, 63, 32, B, P, nullptr, true);
    add ("osc2Detune", "Osc 2 Detune",  4, 109, 0, 63, 32, B, P, nullptr, true);
    add ("osc1PW",     "Osc 1 PW",      5,  94, 0, 127, 0, C, P);
    add ("osc2PW",     "Osc 2 PW",      6,  95, 0, 127, 0, C, P);
    add ("osc1Tri",    "Osc 1 Tri",     7, 102, 0, 1, 0, S, P, onOff);
    add ("osc2Tri",    "Osc 2 Tri",     8, 103, 0, 1, 0, S, P, onOff);
    add ("osc1Saw",    "Osc 1 Saw",     9, 104, 0, 1, 1, S, P, onOff);
    add ("osc2Saw",    "Osc 2 Saw",    10, 105, 0, 1, 1, S, P, onOff);
    add ("osc1Pulse",  "Osc 1 Pulse",  11, 106, 0, 1, 0, S, P, onOff);
    add ("osc2Pulse",  "Osc 2 Pulse",  12, 107, 0, 1, 0, S, P, onOff);
    add ("osc1On",     "Osc 1 On",     13,   9, 0, 1, 1, S, P, onOff);
    add ("osc2On",     "Osc 2 On",     14,  10, 0, 1, 1, S, P, onOff);
    add ("osc1Level",  "Osc 1 Level",  15,  40, 0, 127, 127, C, P);
    add ("osc2Level",  "Osc 2 Level",  16,  41, 0, 127, 127, C, P);
    add ("subOn",      "Sub Osc On",   17,  14, 0, 1, 0, S, P, onOff);
    add ("subLevel",   "Sub Osc Level",18,  42, 0, 127, 127, C, P);
    add ("noiseOn",    "Noise On",     19,  15, 0, 1, 0, S, P, onOff);
    add ("noiseType",  "Noise Type",   20, 108, 0, 1, 0, L, P, "White|Pink"); // NRPN says 0-127; the TEO-5 stores 0-1
    add ("noiseLevel", "Noise Level",  21,  43, 0, 127, 127, C, P);
    add ("osc1Glide",  "Osc 1 Glide",  22,  65, 0, 127, 0, C, P);
    add ("osc2Glide",  "Osc 2 Glide",  23,  66, 0, 127, 0, C, P);
    add ("osc1KeyOn",  "Osc 1 Key On", 24,  87, 0, 1, 1, S, P, onOff);
    add ("osc2KeyOn",  "Osc 2 Key On", 25,  88, 0, 1, 1, S, P, onOff);
    add ("xMod",       "X-Mod Amount", 26,  44, 0, 127, 0, C, P);
    add ("oscSync",    "Osc 1 Sync",   27,  39, 0, 1, 0, S, P, onOff);
    add ("osc2FiltBypass", "Osc 2 Filter Bypass", 28, 117, 0, 1, 0, S, P, onOff);
    add ("glideMode",  "Portamento Mode", 30, 5, 0, 3, 0, L, P, glideModes);
    add ("glideOn",    "Glide On",     31,  68, 0, 1, 0, S, P, onOff);
    add ("pbUp",       "Pitch Bend Up",   32, 85, 0, 12, 2, C, P);
    add ("pbDown",     "Pitch Bend Down", 33, 86, 0, 24, 2, C, P);

    // ---- Filter -----------------------------------------------------------------------------
    // NRPN 35 "Filter Cutoff MSB 0-7" and 39 "Filter State MSB 0-127" are listed separately in the spec;
    // we treat 34/38 as the full-resolution values sent as 14-bit NRPN data. TODO: verify.
    add ("cutoff",     "Filter Cutoff",  34, 33, 0, 1023, 1023, C, P); // spec 0-1024; measured 0-1023
    add ("resonance",  "Filter Res",     36, 34, 0, 255, 0, C, P); // spec 0-265; measured 0-255
    add ("filterBP",   "Filter Bandpass",37,110, 0, 1, 0, S, P, onOff);
    add ("filterState","Filter State",   38, 35, 0, 511, 0, C, P); // spec 0-512; measured 0-511
    add ("filterKeyAmt","Filter Key Amt",40, 36, 0, 127, 0, C, P);

    // ---- Effects ----------------------------------------------------------------------------
    add ("fxOn",       "FX On",        42, 16, 0, 1, 0, S, P, onOff);
    add ("fxType",     "FX Type",      43, 17, 0, 12, 4, L, P, fxTypes, true);
    add ("fxMix",      "FX Depth/Mix", 44, 19, 0, 127, 64, C, P);
    add ("fxTime",     "FX Time",      45, 18, 0, 127, 64, C, P);
    add ("fxMisc",     "FX Fback/Misc",46, 20, 0, 127, 0, C, P);
    add ("fxSync",     "FX Clk Sync",  47, 21, 0, 1, 0, S, P, onOff);
    add ("fxSyncRate", "FX Sync Rate", 48, 22, 0, 10, 0, C, P, nullptr, true);
    add ("revOn",      "Reverb On",    50, 23, 0, 1, 0, S, P, onOff);
    add ("revMix",     "Reverb Mix",   52, 24, 0, 127, 32, C, P);
    add ("revSize",    "Reverb Size",  53, 25, 0, 127, 64, C, P);
    add ("revPredelay","Reverb PreDelay", 54, 26, 0, 127, 0, C, P);
    add ("revDecay",   "Reverb Decay", 55, 27, 0, 127, 64, C, P);
    add ("revTone",    "Reverb Tone",  56, 28, 0, 127, 64, B, P);

    // ---- LFOs -------------------------------------------------------------------------------
    add ("lfo1Freq",   "LFO 1 Freq",   58, 75, 0, 127, 64, C, P);
    add ("lfo2Freq",   "LFO 2 Freq",   59, 80, 0, 127, 64, C, P);
    add ("lfo1Amt",    "LFO 1 Amt",    60, 76, 0, 254, 0, C, P, nullptr, true); // spec 0-256; measured 0-254
    add ("lfo2Amt",    "LFO 2 Amt",    61, 81, 0, 254, 0, C, P, nullptr, true);
    add ("lfo1Shape",  "LFO 1 Shape",  62, 77, 0, 4, 0, L, P, lfoShapes);
    add ("lfo2Shape",  "LFO 2 Shape",  63, 82, 0, 4, 0, L, P, lfoShapes);
    add ("lfo1Sync",   "LFO 1 Sync",   64, 78, 0, 1, 0, S, P, onOff);
    add ("lfo2Sync",   "LFO 2 Sync",   65, 83, 0, 1, 0, S, P, onOff);
    add ("lfo1Dest",   "LFO 1 Dest",   66, -1, 0, 61, 0, L, P, nullptr, true);
    add ("lfo2Dest",   "LFO 2 Dest",   67, -1, 0, 61, 0, L, P, nullptr, true);
    add ("lfo1FreqSync","LFO 1 Freq Sync", 68, 111, 0, 15, 0, C, P, nullptr, true);
    add ("lfo2FreqSync","LFO 2 Freq Sync", 69, 112, 0, 15, 0, C, P, nullptr, true);
    add ("lfo1NoteReset","LFO 1 Note Reset", 70, 79, 0, 1, 0, S, P, onOff);
    add ("lfo2NoteReset","LFO 2 Note Reset", 71, 84, 0, 1, 0, S, P, onOff);
    add ("lfo1Slew",   "LFO 1 Slew",   72, 92, 0, 127, 0, C, P);
    add ("lfo2Slew",   "LFO 2 Slew",   73, 93, 0, 127, 0, C, P);

    // ---- Envelopes --------------------------------------------------------------------------
    add ("env1Amt",    "Env 1 Amt",    75, 50, 0, 254, 127, B, P, nullptr, true); // spec 0-256; measured 0-254
    add ("env2Amt",    "Env 2 Amt",    76, 57, 0, 254, 254, C, P, nullptr, true);
    add ("env1Vel",    "Env 1 Vel On", 77, 51, 0, 1, 0, S, P, onOff);
    add ("env2Vel",    "Env 2 Vel On", 78, 58, 0, 1, 0, S, P, onOff);
    add ("env1Delay",  "Env 1 Delay",  79, 45, 0, 127, 0, C, P);
    add ("env2Delay",  "Env 2 Delay",  80, 52, 0, 127, 0, C, P);
    add ("env1Attack", "Env 1 Attack", 81, 46, 0, 255, 0, C, P);
    add ("env2Attack", "Env 2 Attack", 82, 53, 0, 255, 0, C, P);
    add ("env1Decay",  "Env 1 Decay",  83, 47, 0, 255, 128, C, P);
    add ("env2Decay",  "Env 2 Decay",  84, 54, 0, 255, 128, C, P);
    add ("env1Sustain","Env 1 Sustain",85, 48, 0, 127, 64, C, P);
    add ("env2Sustain","Env 2 Sustain",86, 55, 0, 127, 127, C, P);
    add ("env1Release","Env 1 Release",87, 49, 0, 255, 64, C, P);
    add ("env2Release","Env 2 Release",88, 56, 0, 255, 64, C, P);
    add ("envRoute",   "Env Routing",  89, 69, 0, 2, 0, L, P, envRoutes, true);
    add ("env1Dest",   "Env 1 Aux Dest", 90, -1, 0, 61, 0, L, P, nullptr, true);
    add ("envRepeat",  "Env Repeat",   91,116, 0, 3, 0, L, P, envRepeats);

    // ---- Voice / misc -----------------------------------------------------------------------
    add ("voiceVolume","Program Volume", 93, 12, 0, 127, 100, C, P);
    add ("overdrive",  "Overdrive",    95, 67, 0, 127, 0, C, P);
    add ("vintage",    "Vintage",      96, 37, 0, 127, 0, C, P);
    add ("unisonOn",   "Unison On",    97, 31, 0, 1, 0, S, P, onOff);
    add ("unisonVoices","Unison Voices", 98, 70, 0, 5, 0, L, P, unisonVoiceNames, true);
    add ("unisonDetune","Unison Detune", 99, 71, 0, 7, 0, C, P);
    for (int i = 0; i < 5; ++i)
    {
        static const char* ids[]   = { "unisonNote1", "unisonNote2", "unisonNote3", "unisonNote4", "unisonNote5" };
        static const char* names[] = { "Unison Note 1", "Unison Note 2", "Unison Note 3", "Unison Note 4", "Unison Note 5" };
        add (ids[i], names[i], 100 + i, -1, 0, 43, 0, C, P);
    }

    // ---- Mod matrix (16 slots) --------------------------------------------------------------
    static juce::StringArray modIds, modNames; // keep the C strings alive
    if (modIds.isEmpty())
    {
        for (int i = 1; i <= 16; ++i)
        {
            modIds.add ("mod" + juce::String (i) + "Src");  modNames.add ("Mod " + juce::String (i) + " Source");
            modIds.add ("mod" + juce::String (i) + "Amt");  modNames.add ("Mod " + juce::String (i) + " Amt");
            modIds.add ("mod" + juce::String (i) + "Dest"); modNames.add ("Mod " + juce::String (i) + " Dest");
        }
    }
    for (int i = 0; i < 16; ++i)
    {
        add (modIds[i * 3].toRawUTF8(),     modNames[i * 3].toRawUTF8(),     105 + i, -1, 0, 19, 0, L, P);
        // Spec says 0-255; the TEO-5 stores 0-254 with 127 = no modulation.
        add (modIds[i * 3 + 1].toRawUTF8(), modNames[i * 3 + 1].toRawUTF8(), 121 + i, -1, 0, 254, 127, B, P);
        add (modIds[i * 3 + 2].toRawUTF8(), modNames[i * 3 + 2].toRawUTF8(), 137 + i, -1, 0, 64, 0, L, P);
    }

    // ---- Keyboard / clock / arp -------------------------------------------------------------
    add ("keyMode",    "Unison Key Mode", 153, 72, 0, 2, 2, L, P, keyModes);
    add ("envRetrig",  "Unison Env Retrig", 154, 73, 0, 1, 0, S, P, onOff);
    // Measured 0-65: 0 = use the global scale, 1-65 = the built-in tunings.
    add ("scale",      "Scale",        155, 114, 0, 65, 0, L, P, tuningChoices (true));
    add ("transpose",  "Transpose",    156, 115, 0, 4, 2, L, P, transposes, true);
    add ("clockBpm",   "Clock BPM",    157, 3, 30, 250, 120, C, P, nullptr, true); // CC 3 range is 15-127
    add ("clockDiv",   "Clock Divide", 158, 63, 0, 7, 4, L, P, clockDivs);
    add ("keySplit",   "Key Split",    180, 29, 0, 1, 0, S, P, onOff);
    add ("keySplit2",  "Key Split 2",  181, 30, 0, 1, 0, S, P, onOff);
    add ("keySplitNote","Key Split Note", 182, 89, 0, 43, 0, C, P);
    add ("arpOn",      "Arp On",       183, 59, 0, 1, 0, S, P, onOff);
    add ("arpMode",    "Arp Mode",     184, 60, 0, 4, 2, L, P, arpModes);
    add ("arpRange",   "Arp Octave",   185, 61, 0, 2, 0, L, P, arpRanges);
    add ("arpRepeat",  "Arp Repeat",   186, 62, 0, 3, 0, L, P, arpRepeats);
    add ("arpRelatch", "Arp Relatch",  187, -1, 0, 1, 0, S, P, onOff);

    // ---- Control (not saved in program) -----------------------------------------------------
    add ("seqPlay",    "Seq Play",     4198, 90, 0, 1, 0, S, X, onOff);
    // Measured: the TEO-5 sends NRPN 4199 when its RECORD is pressed (not in the spec; next to Seq Play's 4198).
    add ("seqRecord",  "Seq Record",   4199, 91, 0, 1, 0, S, X, onOff);
    add ("masterVolume","Master Volume", -1,  7, 0, 127, 100, C, X);

    // ---- Panel-only state -------------------------------------------------------------------
    add ("lfoSelect",  "LFO Select",   -1, -1, 0, 1, 0, L, U, "LFO 1|LFO 2");
    add ("hold",       "Hold",         -1, -1, 0, 1, 0, S, U, onOff, true); // no NRPN/CC documented

    // ---- Globals (not automatable; shown in the Global page) --------------------------------
    // Measured on a real TEO-5 (the spec swaps the two ranges): coarse 0-24 = -12..+12 semitones, fine 0-100 = -50..+50 cents.
    add ("gCoarseTune",  "Master Coarse",      4096, -1, 0, 24, 12, B, G);
    add ("gFineTune",    "Master Fine",        4097, -1, 0, 100, 50, B, G);
    add ("gMidiChannel", "MIDI Channel",       4098, -1, 0, 16, 0, L, G, "All|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16");
    // The spec's table lists 0-3; the synth has the six modes the user's guide lists (5 = In No S/S measured).
    add ("gClockMode",   "Clock Mode",         4099, -1, 0, 5, 0, L, G, "Off|Out|Out No S/S|In|In Thru|In No S/S");
    add ("gClockCableIn","Clock Cable In",     4100, -1, 0, 1, 0, L, G, "MIDI|USB");
    add ("gClockCableOut","Clock Cable Out",    4101, -1, 0, 3, 0, L, G, "None|MIDI|USB|All");
    // Value orders below follow the spec's prose, not its table: a TEO-5 sending NRPN reports 2 for Param Send.
    add ("gParamSend",   "MIDI Param Send",    4102, -1, 0, 2, 2, L, G, "Off|CC|NRPN");
    add ("gParamRecv",   "MIDI Param Receive", 4103, -1, 0, 2, 2, L, G, "Off|CC|NRPN");
    add ("gMidiControl", "MIDI Control On",    4104, -1, 0, 1, 1, S, G, onOff);
    add ("gSysexEnable", "MIDI SysEx On",      4105, -1, 0, 1, 1, S, G, onOff);
    add ("gSysexCable",  "MIDI SysEx Cable",   4106, -1, 0, 1, 1, L, G, "MIDI|USB");
    add ("gCableOut",    "MIDI Out Select",    4107, -1, 0, 3, 3, L, G, "Off|MIDI|USB|All");
    add ("gProgSend",    "MIDI Program Send",  4108, -1, 0, 1, 1, S, G, onOff);
    add ("gProgRecv",    "MIDI Program Receive", 4109, -1, 0, 1, 1, S, G, onOff);
    add ("gArpNotes",    "MIDI Arp Notes",     4110, -1, 0, 1, 0, S, G, onOff);
    add ("gLocalControl","Local Control",      4111, -1, 0, 2, 2, L, G, "All Off|Key/Wheels Off|On");
    add ("gStereo",      "Mono/Stereo",        4112, -1, 0, 1, 1, L, G, "Mono|Stereo");
    add ("gPotMode",     "Pot Mode",           4113, -1, 0, 2, 0, L, G, "Relative|Passthru|Jump");
    add ("gPedalAssign", "Pedal Function",     4114, -1, 0, 4, 1, L, G, "Breath CC2|Foot CC4|Exp CC11|LPF Full|LPF Half");
    add ("gSusPolarity", "Pedal Polarity",     4115, -1, 0, 1, 0, L, G, "Normal|Reversed");
    add ("gSusMode",     "Foot Function",      4116, -1, 0, 3, 0, L, G, "Sustain|Arp Hold|Arp Hold Mom.|Seq Start/Stop");
    add ("gScale",       "Scale (Alt Tuning)", 4117, -1, 0, 64, 0, L, G, tuningChoices (false));
    add ("gPressure",    "Aftertouch On",      4118, -1, 0, 1, 1, S, G, onOff);
    add ("gPressureCurve","Aftertouch Curve",  4119, -1, 0, 7, 0, L, G, "Curve 1|Curve 2|Curve 3|Curve 4|Curve 5|Curve 6|Curve 7|Curve 8");
    // Measured 0-6 (the spec says 0-7; the user's guide lists seven curves).
    add ("gVelocityCurve","Velocity Curve",    4120, -1, 0, 6, 0, L, G, "Curve 1|Curve 2|Curve 3|Curve 4|Curve 5|Curve 6|Curve 7");
    add ("gScreensaver", "Screen Saver",       4121, -1, 0, 1, 1, S, G, onOff);
    add ("gArpBeatSync", "Arp Beat Sync",      4122, -1, 0, 1, 0, L, G, "Off|Quantize");

    return t;
}
} // namespace

const juce::Array<ParamDef>& allParams()
{
    static const juce::Array<ParamDef> table = buildTable();
    return table;
}

const ParamDef* findById (const juce::String& id)
{
    for (auto& p : allParams())
        if (id == p.id)
            return &p;
    return nullptr;
}

const ParamDef* findByNrpn (int nrpn)
{
    if (nrpn < 0)
        return nullptr;
    for (auto& p : allParams())
        if (p.nrpn == nrpn)
            return &p;
    return nullptr;
}

const ParamDef* findByCc (int cc)
{
    if (cc < 0)
        return nullptr;
    for (auto& p : allParams())
        if (p.cc == cc)
            return &p;
    return nullptr;
}

const juce::StringArray& modSourceNames()
{
    static const juce::StringArray names {
        "Off", "Osc 2", "Noise", "LFO 1", "LFO 2", "Env 1", "Env 2", "Voice Spread", "Pitch Bend", "Mod Wheel",
        "Pressure", "Breath", "Foot Pedal", "Expression Pedal", "Velocity", "Note Number", "Filter Out", "Random",
        "DC", "Audio Out"
    };
    return names;
}

const juce::StringArray& modDestinationNames()
{
    static const juce::StringArray names = []
    {
        juce::StringArray n {
            "No Dest", "Osc 1 Frequency", "Osc 2 Frequency", "Osc All Frequency", "Osc 1 Detune", "Osc 2 Detune",
            "Osc All Detune", "Osc 1 Pulse Width", "Osc 2 Pulse Width", "Osc All Pulse Width", "Osc 1 Level",
            "Osc 2 Level", "Sub Level", "Noise", "X-Mod Level", "Cutoff", "Resonance", "Filter State", "FX Mix",
            "FX Time", "FX Misc", "Reverb Mix", "Reverb Size", "Reverb PreDelay", "Reverb Decay", "Reverb Tone",
            "LFO 1 Freq", "LFO 2 Freq", "LFO All Freq", "LFO 1 Amt", "LFO 2 Amt", "LFO All Amt", "Env 1 Amt",
            "Env 2 Amt", "Env 1 Delay", "Env 2 Delay", "Env 1 Attack", "Env 2 Attack", "Env 1 Decay", "Env 2 Decay",
            "Env 1 Sustain", "Env 2 Sustain", "Env 1 Release", "Env 2 Release", "Voice Volume", "Panning",
            "Overdrive", "Vintage Amount", "Unison Detune"
        };
        for (int i = 1; i <= 16; ++i)
            n.add ("Mod " + juce::String (i) + " Amount");
        return n;
    }();
    return names;
}

juce::StringArray choiceNames (const ParamDef& def)
{
    juce::String id (def.id);
    if (id.startsWith ("mod") && id.endsWith ("Src"))
        return modSourceNames();
    // LFO/Env 1 destinations are 0-61, i.e. assumed to be the first 62 mod destinations. TODO: verify.
    if (id.endsWith ("Dest"))
    {
        juce::StringArray n = modDestinationNames();
        n.removeRange (def.maxValue + 1, n.size());
        return n;
    }
    if (def.choices != nullptr)
        return juce::StringArray::fromTokens (def.choices, "|", "");
    return {};
}

juce::String valueToText (const ParamDef& def, int value)
{
    if (def.kind == ParamKind::Choice || def.kind == ParamKind::Switch)
    {
        auto names = choiceNames (def);
        const int idx = value - def.minValue;
        if (juce::isPositiveAndBelow (idx, names.size()))
            return names[idx];
    }
    if (def.kind == ParamKind::Bipolar)
    {
        const int centre = (def.minValue + def.maxValue + 1) / 2;
        const int v = value - centre;
        return (v > 0 ? "+" : "") + juce::String (v);
    }
    return juce::String (value);
}

} // namespace teo5
