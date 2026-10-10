// Headless Serum 1 (VST3) host.
//   s1host probe <state.bin>             write the default state, list params
//   s1host dump <list.txt> <outDir>      for each .fxp path in list: load chunk, dump params as JSON
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <map>

using namespace juce;

static double g_sr = 44100.0;
static int g_block = 512;
static std::unique_ptr<AudioPluginInstance> loadSerum()
{
    AudioPluginFormatManager fm;
    fm.addFormat (new VST3PluginFormat());
    OwnedArray<PluginDescription> descs;
    VST3PluginFormat vst3;
    vst3.findAllTypesForFile (descs, "/Library/Audio/Plug-Ins/VST3/Serum.vst3");
    if (descs.isEmpty()) { std::cerr << "no Serum.vst3 types\n"; return nullptr; }
    String err;
    auto p = fm.createPluginInstance (*descs[0], g_sr, g_block, err);
    if (! p) std::cerr << "load failed: " << err << "\n";
    else { p->setPlayConfigDetails (0, 2, g_sr, g_block); p->prepareToPlay (g_sr, g_block); }
    return p;
}

// The IComponent payload inside JUCE's VST3 state wrapper.
static MemoryBlock componentState (AudioPluginInstance& p)
{
    MemoryBlock mb; p.getStateInformation (mb);
    auto xml = AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    MemoryBlock comp;
    if (xml) if (auto* c = xml->getChildByName ("IComponent")) comp.fromBase64Encoding (c->getAllSubText());
    return comp;
}

static void setComponentState (AudioPluginInstance& p, const MemoryBlock& comp)
{
    MemoryBlock mb; p.getStateInformation (mb);
    auto xml = AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    auto* c = xml->getChildByName ("IComponent");
    c->deleteAllTextElements();
    c->addTextElement (comp.toBase64Encoding());
    if (auto* e = xml->getChildByName ("IEditController")) xml->removeChildElement (e, true);
    MemoryBlock out; AudioProcessor::copyXmlToBinary (*xml, out);
    p.setStateInformation (out.getData(), (int) out.getSize());
}

static var paramsJson (AudioPluginInstance& p)
{
    Array<var> arr;
    for (auto* prm : p.getParameters())
    {
        DynamicObject::Ptr o = new DynamicObject();
        o->setProperty ("i", prm->getParameterIndex());
        o->setProperty ("name", prm->getName (64));
        o->setProperty ("v", prm->getValue());
        o->setProperty ("text", prm->getText (prm->getValue(), 64) + (prm->getLabel().isNotEmpty() ? " " + prm->getLabel() : String()));
        arr.add (var (o.get()));
    }
    return arr;
}


// ---- wavetable capture -------------------------------------------------------
// One cycle of MIDI note 36 is exactly 2048 samples at this rate.
static const double kCapNoteHz = 440.0 * std::pow (2.0, (36 - 69) / 12.0);

struct Cap
{
    AudioPluginInstance& p;
    std::map<String, AudioProcessorParameter*> byName;
    std::vector<float> initV;   // normalized values of Serum's init patch
    explicit Cap (AudioPluginInstance& pi) : p (pi)
    {
        for (auto* prm : p.getParameters()) byName[prm->getName (64)] = prm;
    }
    void set (const String& n, float v) { if (auto* q = byName[n]) q->setValueNotifyingHost (v); }
    void init (const String& n) { if (auto* q = byName[n]) q->setValueNotifyingHost (initV[(size_t) q->getParameterIndex()]); }
    float get (const String& n) { auto* q = byName[n]; return q ? q->getValue() : 0.0f; }
    void render (int samples, std::vector<float>* outL = nullptr)
    {
        AudioBuffer<float> buf (2, g_block);
        MidiBuffer midi;
        while (samples > 0)
        {
            const int n = jmin (samples, g_block);
            buf.setSize (2, n, false, false, true);
            buf.clear();
            p.processBlock (buf, midi);
            if (outL) outL->insert (outL->end(), buf.getReadPointer (0), buf.getReadPointer (0) + n);
            samples -= n;
        }
    }
    void note (int nn, bool on)
    {
        AudioBuffer<float> buf (2, 64);
        MidiBuffer midi;
        midi.addEvent (on ? MidiMessage::noteOn (1, nn, (uint8) 127) : MidiMessage::noteOff (1, nn), 0);
        buf.clear();
        p.processBlock (buf, midi);
        p.processBlock (buf, midi = {});
    }
};

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::cerr << "usage\n"; return 2; }
    if (String (argv[1]) == "capture") g_sr = 2048.0 * kCapNoteHz;
    if (String (argv[1]) == "render" || String (argv[1]) == "srcprobe" || String (argv[1]) == "envprobe" || String (argv[1]) == "mixprobe" || String (argv[1]) == "polyprobe") g_sr = 48000.0;
    auto p = loadSerum();
    if (! p) return 1;
    String mode (argv[1]);
    if (mode == "render" && argc >= 4)
    {
        // s1host render <preset.fxp> <out.wav>: the shared audition phrase at 48 kHz.
        MemoryBlock fxp; File (argv[2]).loadFileAsData (fxp);
        auto* d = (const uint8*) fxp.getData();
        auto n = (size_t) ByteOrder::bigEndianInt (d + 56);
        setComponentState (*p, MemoryBlock (d + 60, jmin (n, fxp.getSize() - 60)));
        for (int k = 4; k < argc; ++k)   // name=normalizedValue overrides
            if (String (argv[k]).contains ("="))
            {
                Cap c (*p);
                c.set (String (argv[k]).upToFirstOccurrenceOf ("=", false, false), String (argv[k]).fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            }
        if (argc >= 5 && String (argv[4]) == "dry")
        {
            // Diagnostic: oscillators + envelopes only (filter and FX off).
            Cap c (*p);
            c.set ("Filter On", 0);
            for (auto fx : { "Dist", "Flg", "Phs", "Cho", "Dly", "Comp", "Rev", "EQ", "FX Fil", "Hyp" }) c.set (String (fx) + " Enable", 0);
        }
        // {start s, end s, note}; mirrors the WT-1 renderer.
        // Single notes, a chord, then an overlapping run (exposes mono/legato).
        const double ev[][3] = { {0.0, 1.0, 48}, {1.25, 2.25, 55}, {2.5, 4.5, 48}, {2.5, 4.5, 52}, {2.5, 4.5, 55}, {2.5, 4.5, 59},
                                 {6.0, 6.7, 48}, {6.5, 7.2, 51}, {7.0, 7.9, 55} };
        const double total = 9.5;
        { Cap pre (*p); pre.render ((int) (0.5 * g_sr)); }   // pre-roll, as in the WT-1 renderer
        MidiBuffer all;
        for (auto& e : ev)
        {
            all.addEvent (MidiMessage::noteOn (1, (int) e[2], (uint8) 100), (int) (e[0] * g_sr) + 256);
            all.addEvent (MidiMessage::noteOff (1, (int) e[2]), (int) (e[1] * g_sr) + 256);
        }
        const int N = (int) (total * g_sr);
        AudioBuffer<float> out (2, N), buf (2, g_block);
        for (int pos = 0; pos < N; pos += g_block)
        {
            const int len = jmin (g_block, N - pos);
            buf.setSize (2, len, false, false, true); buf.clear();
            MidiBuffer m;
            for (const auto meta : all)
                if (meta.samplePosition >= pos && meta.samplePosition < pos + len)
                    m.addEvent (meta.getMessage(), meta.samplePosition - pos);
            p->processBlock (buf, m);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, len);
        }
        File f (argv[3]); f.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), g_sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (out, 0, N);
        return 0;
    }
    if (mode == "srcprobe" && argc >= 3)
    {
        // s1host srcprobe <base.fxp>: for each source id, route slot 1 = src -> A Vol
        // (base 0, depth +100) and print the source value per 50 ms window,
        // recovered from Serum's knob^2 volume law.
        Cap c (*p);
        for (auto* prm : p->getParameters()) c.initV.push_back (prm->getValue());
        MemoryBlock fxp; File (argv[2]).loadFileAsData (fxp);
        auto* d = (const uint8*) fxp.getData();
        auto n = (size_t) ByteOrder::bigEndianInt (d + 56);
        MemoryBlock orig (d + 60, jmin (n, fxp.getSize() - 60));
        // Split the first zlib stream from the rest of the chunk.
        juce::MemoryOutputStream mainOut;
        juce::MemoryInputStream in (orig, false);
        juce::GZIPDecompressorInputStream gz (&in, false, juce::GZIPDecompressorInputStream::zlibFormat);
        mainOut.writeFromInputStream (gz, -1);
        MemoryBlock mainRaw = mainOut.getMemoryBlock();
        // Locate where the first stream ends by recompressing is not exact; find the
        // second stream header (0x78) after the first stream using zlib's own size.
        // The decompressor reads ahead, so the caller passes the first stream's size.
        const int64 firstLen = String (argv[3]).getLargeIntValue();
        auto run = [&] (int src, bool ref) -> std::vector<float>
        {
            MemoryBlock m = mainRaw;
            auto* b = (uint8*) m.getData();
            const int mk = 32;  // slot 1 marker
            int32 sv = src; memcpy (b + mk - 12, &sv, 4);
            uint16 ids[2] = { 0, 1 }; memcpy (b + mk - 8, ids, 4);
            juce::MemoryOutputStream z;
            { juce::GZIPCompressorOutputStream gzo (z, 6); gzo.write (m.getData(), m.getSize()); }
            MemoryBlock chunk = z.getMemoryBlock();
            chunk.append ((const uint8*) orig.getData() + firstLen, orig.getSize() - (size_t) firstLen);
            setComponentState (*p, chunk);
            c.set ("Osc A On", 1); c.set ("Osc B On", 0); c.set ("Osc N On", 0); c.set ("Osc S On", 0); c.set ("Filter On", 0);
            for (auto fx : { "Dist", "Flg", "Phs", "Cho", "Dly", "Comp", "Rev", "EQ", "FX Fil", "Hyp" }) c.set (String (fx) + " Enable", 0);
            for (int k = 1; k <= 32; ++k) c.set (k < 10 ? "Mod " + String (k) + " amt" : "Mod" + String (k) + " amt", 0.5f);
            for (auto q : { "A Octave", "A Semi", "A Fine", "A Unison", "A Pan", "Env1 Atk", "Env1 Hold", "Env1 Dec", "Env1 Sus", "Env1 Rel", "MasterVol", "Mast.Tun" }) c.init (q);
            c.set ("Mod 1 amt", ref ? 0.5f : 1.0f); c.set ("Mod 1 out", 1.0f);
            c.set ("A Vol", ref ? 1.0f : 0.0f);
            const bool alt = argc >= 5;
            c.set ("Mod Wheel", alt ? 0.3f : 0.5f);
            c.set ("Macro 1", 0.2f); c.set ("Macro 2", 0.4f); c.set ("Macro 3", 0.6f); c.set ("Macro 4", 0.8f);
            c.set ("Env2 Atk", 0.5f); c.set ("Env2 Sus", 1.0f); c.set ("Env3 Atk", 0.25f); c.set ("Env3 Sus", 1.0f);
            c.render (4800);
            {
                AudioBuffer<float> buf (2, 64); MidiBuffer midi;
                midi.addEvent (MidiMessage::noteOn (1, alt ? 72 : 60, (uint8) (alt ? 64 : 127)), 0);
                buf.clear(); p->processBlock (buf, midi);
            }
            std::vector<float> L; c.render (72000, &L);
            c.note (alt ? 72 : 60, false); c.render (48000);
            std::vector<float> rms;
            for (size_t i = 0; i + 2400 <= L.size(); i += 2400)
            {
                double e = 0; for (size_t j = i; j < i + 2400; ++j) e += L[j] * L[j];
                rms.push_back ((float) std::sqrt (e / 2400));
            }
            return rms;
        };
        auto refR = run (0, true);
        for (int src = 1; src <= 24; ++src)
        {
            auto r = run (src, false);
            std::cout << "src " << src << ":";
            for (size_t i = 0; i < r.size(); i += 2)
                std::cout << " " << String (std::sqrt (r[i] / jmax (1e-9f, refR[i])), 2);
            std::cout << "\n";
        }
        return 0;
    }
    if (mode == "envprobe")
    {
        // s1host envprobe <atk> <dec> <sus> <rel> [base.fxp]: normalized
        // Env1 values on the init patch; prints the amplitude per 10 ms
        // (peak-normalized RMS of osc A, held 2 s then released).
        Cap c (*p);
        for (auto* prm : p->getParameters()) c.initV.push_back (prm->getValue());
        if (argc >= 7)
        {
            // Base preset supplies the stored curves; neutralize the rest of the voice.
            MemoryBlock fxp; File (argv[6]).loadFileAsData (fxp);
            auto* d = (const uint8*) fxp.getData();
            auto n = (size_t) ByteOrder::bigEndianInt (d + 56);
            setComponentState (*p, MemoryBlock (d + 60, jmin (n, fxp.getSize() - 60)));
            c.set ("Osc A On", 1); c.set ("Osc B On", 0); c.set ("Osc N On", 0); c.set ("Osc S On", 0); c.set ("Filter On", 0);
            for (auto fx : { "Dist", "Flg", "Phs", "Cho", "Dly", "Comp", "Rev", "EQ", "FX Fil", "Hyp" }) c.set (String (fx) + " Enable", 0);
            for (int m = 1; m <= 32; ++m) c.init (m < 10 ? "Mod " + String (m) + " amt" : "Mod" + String (m) + " amt");
            for (auto q : { "A Octave", "A Semi", "A Fine", "A CoarsePit", "A Vol", "A Unison", "A Warp", "MasterVol", "Mast.Tun", "Env1 Hold", "PortTime" }) c.init (q);
        }
        c.set ("Env1 Atk", String (argv[2]).getFloatValue()); c.set ("Env1 Dec", String (argv[3]).getFloatValue());
        c.set ("Env1 Sus", String (argv[4]).getFloatValue()); c.set ("Env1 Rel", String (argv[5]).getFloatValue());
        std::cerr << "atk " << c.byName["Env1 Atk"]->getCurrentValueAsText() << " dec " << c.byName["Env1 Dec"]->getCurrentValueAsText()
                  << " sus " << c.byName["Env1 Sus"]->getCurrentValueAsText() << " rel " << c.byName["Env1 Rel"]->getCurrentValueAsText()
                  << " curves " << c.byName["A curve1"]->getCurrentValueAsText() << "/" << c.byName["D curve1"]->getCurrentValueAsText() << "/" << c.byName["R curve1"]->getCurrentValueAsText() << "\n";
        c.render (4800);
        c.note (72, true);
        std::vector<float> L; c.render (96000, &L);
        c.note (72, false); c.render (96000, &L);
        for (size_t i = 0; i + 480 <= L.size(); i += 480)
        {
            double e = 0; for (size_t j = i; j < i + 480; ++j) e += L[j] * L[j];
            std::cout << std::sqrt (e / 480) << "\n";
        }
        return 0;
    }
    if (mode == "mixprobe")
    {
        // s1host mixprobe: RMS of osc A, sub and noise alone on the init patch
        // at several knob values (note 48, 0.5 s after note-on).
        Cap c (*p);
        for (auto* prm : p->getParameters()) c.initV.push_back (prm->getValue());
        auto rms = [&] (const char* on, const char* lvl, float v)
        {
            for (auto o : { "Osc A On", "Osc B On", "Osc N On", "Osc S On" }) c.set (o, 0);
            c.set (on, 1); c.set (lvl, v);
            c.render (4800); c.note (48, true); c.render (24000);
            std::vector<float> L; c.render (24000, &L); c.note (48, false); c.render (48000);
            double e = 0; for (auto x : L) e += x * x;
            return std::sqrt (e / L.size());
        };
        for (float v : { 0.25f, 0.5f, 0.75f, 1.0f })
            std::cout << "v " << v << "  oscA " << rms ("Osc A On", "A Vol", v) << "  sub " << rms ("Osc S On", "Sub Osc Level", v)
                      << "  noise " << rms ("Osc N On", "Noise Level", v) << "\n";
        return 0;
    }
    if (mode == "polyprobe" && argc >= 3)
    {
        // s1host polyprobe <list.txt>: per preset "keep48 envR2 envR3 envR1" where
        // keep48 = energy at C3's octave family while G3 also holds, relative to
        // C3 alone (0 = mono); env* are 10 ms RMS curves from G3's onset (R2: C3
        // then G3, R3: G3 from silence, R1: C3 alone continuing).
        StringArray list; list.addLines (File (argv[2]).loadFileAsString()); list.removeEmptyStrings();
        auto play = [&] (const MemoryBlock& chunk, std::vector<std::pair<double, int>> ons, double total)
        {
            setComponentState (*p, chunk);
            Cap c (*p);
            if (argc >= 4)  // "clean": no modulation, filter or FX
            {
                c.set ("Filter On", 0);
                for (auto fx : { "Dist", "Flg", "Phs", "Cho", "Dly", "Comp", "Rev", "EQ", "FX Fil", "Hyp" }) c.set (String (fx) + " Enable", 0);
                for (int m = 1; m <= 32; ++m) c.set (m < 10 ? "Mod " + String (m) + " amt" : "Mod" + String (m) + " amt", 0.5f);
                c.set ("PortTime", 0);
            }
            c.render (4800);
            const int N = (int) (total * g_sr);
            std::vector<float> L;
            int pos = 0;
            std::sort (ons.begin(), ons.end());
            size_t e = 0;
            while (pos < N)
            {
                const int len = jmin (g_block, N - pos);
                AudioBuffer<float> buf (2, len); buf.clear(); MidiBuffer m;
                while (e < ons.size() && (int) (ons[e].first * g_sr) < pos + len)
                {
                    m.addEvent (MidiMessage::noteOn (1, ons[e].second, (uint8) 100), jmax (0, (int) (ons[e].first * g_sr) - pos));
                    ++e;
                }
                p->processBlock (buf, m);
                L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + len);
                pos += len;
            }
            AudioBuffer<float> buf (2, 64); MidiBuffer off;
            for (int nn : { 48, 55 }) off.addEvent (MidiMessage::noteOff (1, nn), 0);
            buf.clear(); p->processBlock (buf, off);
            c.render ((int) (2.0 * g_sr));
            return L;
        };
        auto goertzel = [&] (const std::vector<float>& x, double t0, double t1, double hz)
        {
            const int a = (int) (t0 * g_sr), b = (int) (t1 * g_sr);
            const double w = 2 * MathConstants<double>::pi * hz / g_sr, cw = 2 * std::cos (w);
            double s1 = 0, s2 = 0;
            for (int i = a; i < b; ++i) { const double s0 = x[(size_t) i] + cw * s1 - s2; s2 = s1; s1 = s0; }
            return s1 * s1 + s2 * s2 - cw * s1 * s2;
        };
        auto family = [&] (const std::vector<float>& x)
        {
            double e = 0;
            for (double hz : { 32.703, 65.406, 130.81, 261.63, 523.25 }) e += goertzel (x, 0.7, 1.2, hz);
            return e;
        };
        auto env = [&] (const std::vector<float>& x)
        {
            String out;
            for (int k = 0; k < 30; ++k)
            {
                const int a = (int) ((0.5 + k * 0.01) * g_sr), b = a + (int) (0.01 * g_sr);
                double e = 0; for (int i = a; i < b; ++i) e += x[(size_t) i] * x[(size_t) i];
                out << String (std::sqrt (e / (b - a)), 5) << (k < 29 ? "," : "");
            }
            return out;
        };
        for (auto& path : list)
        {
            MemoryBlock fxp; File (path).loadFileAsData (fxp);
            auto* d = (const uint8*) fxp.getData();
            auto n = (size_t) ByteOrder::bigEndianInt (d + 56);
            MemoryBlock chunk (d + 60, jmin (n, fxp.getSize() - 60));
            auto r1 = play (chunk, { { 0.0, 48 } }, 1.3);
            auto r2 = play (chunk, { { 0.0, 48 }, { 0.5, 55 } }, 1.3);
            auto r3 = play (chunk, { { 0.5, 55 } }, 1.3);
            const double keep = family (r2) / jmax (1e-12, family (r1));
            std::cout << File (path).getFileNameWithoutExtension() << "\t" << keep << "\t" << env (r2) << "\t" << env (r3) << "\t" << env (r1) << "\n";
        }
        return 0;
    }
    if (mode == "ranges")
    {
        // s1host ranges <name,name,...>: display text at 0, 0.25, 0.5, 0.75, 1
        StringArray names; names.addTokens (argv[2], ",", "");
        for (auto* prm : p->getParameters())
            if (names.contains (prm->getName (64)))
            {
                std::cout << prm->getName (64) << ":";
                for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) std::cout << " [" << prm->getText (v, 64) << "]";
                std::cout << "\n";
            }
        return 0;
    }
    if (mode == "probe")
    {
        auto comp = componentState (*p);
        File (argv[2]).replaceWithData (comp.getData(), comp.getSize());
        std::cout << "params " << p->getParameters().size() << " compState " << comp.getSize() << "\n";
        int n = 0;
        for (auto* prm : p->getParameters())
            if (n++ < 400) std::cout << prm->getParameterIndex() << "\t" << prm->getName (64) << "\t" << prm->getText (prm->getValue(), 64) << "\n";
        return 0;
    }
    if (mode == "dump" && argc >= 4)
    {
        File outDir (argv[3]); outDir.createDirectory();
        StringArray list; list.addLines (File (argv[2]).loadFileAsString()); list.removeEmptyStrings();
        int i = 0;
        for (auto& path : list)
        {
            MemoryBlock fxp; File (path).loadFileAsData (fxp);
            // fxp layout: 60-byte header, then the opaque chunk; pass the whole chunk (zlib stream incl.).
            auto* d = (const uint8*) fxp.getData();
            auto n = (size_t) ByteOrder::bigEndianInt (d + 56);
            MemoryBlock chunk (d + 60, jmin (n, fxp.getSize() - 60));
            setComponentState (*p, chunk);
            DynamicObject::Ptr o = new DynamicObject();
            o->setProperty ("file", path);
            o->setProperty ("program", p->getProgramName (p->getCurrentProgram()));
            o->setProperty ("params", paramsJson (*p));
            auto& ps = p->getParameters();
            o->setProperty ("framesA", ps[11]->getText (1.0f, 64).trim());
            o->setProperty ("framesB", ps[24]->getText (1.0f, 64).trim());
            outDir.getChildFile (String (i++).paddedLeft ('0', 4) + ".json").replaceWithText (JSON::toString (var (o.get()), true));
        }
        std::cout << "dumped " << i << "\n";
        return 0;
    }
    if (mode == "capture" && argc >= 5)
    {
        // s1host capture <list.txt> <outDir> <frames>: per preset, per enabled
        // oscillator, <frames> single cycles across WTPos as float32 frame-major.
        const int K = String (argv[4]).getIntValue();
        File outDir (argv[3]); outDir.createDirectory();
        StringArray list; list.addLines (File (argv[2]).loadFileAsString()); list.removeEmptyStrings();
        Cap c (*p);
        for (auto* prm : p->getParameters()) c.initV.push_back (prm->getValue());
        int idx = 0;
        for (auto& path : list)
        {
            MemoryBlock fxp; File (path).loadFileAsData (fxp);
            auto* d = (const uint8*) fxp.getData();
            auto n = (size_t) ByteOrder::bigEndianInt (d + 56);
            MemoryBlock chunk (d + 60, jmin (n, fxp.getSize() - 60));
            for (int osc = 0; osc < 2; ++osc)
            {
                setComponentState (*p, chunk);
                const String X = osc == 0 ? "A" : "B", O = osc == 0 ? "B" : "A";
                if (c.get (String ("Osc ") + X + " On") < 0.5f) continue;
                // Neutral voice around this oscillator. Warp stays as stored.
                c.set (String ("Osc ") + O + " On", 0); c.set ("Osc N On", 0); c.set ("Osc S On", 0);
                c.set ("Filter On", 0);
                for (auto fx : { "Dist", "Flg", "Phs", "Cho", "Dly", "Comp", "Rev", "EQ", "FX Fil", "Hyp" }) c.set (String (fx) + " Enable", 0);
                for (int m = 1; m <= 32; ++m) c.init (m < 10 ? "Mod " + String (m) + " amt" : "Mod" + String (m) + " amt");
                for (auto q : { " Octave", " Semi", " Fine", " CoarsePit", " Vol", " Pan", " Unison" }) c.init (X + q);
                c.set (X + " RandPhase", 0); c.set (X + " Phase", 0);
                for (auto q : { "Env1 Atk", "Env1 Hold", "Env1 Dec", "Env1 Sus", "Env1 Rel", "Mast.Tun", "PortTime", "MasterVol",
                                "Pitch Bend", "Mod Wheel", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Amp." })
                    c.init (q);
                c.set (X + " WTPos", 0);
                c.render (4096);
                c.note (36, true);
                c.render (2048 * 8);
                std::vector<float> frames;
                for (int k = 0; k < K; ++k)
                {
                    c.set (X + " WTPos", K == 1 ? 0.0f : (float) k / (float) (K - 1));
                    c.render (2048 * 3);
                    c.render (2048, &frames);
                }
                c.note (36, false);
                c.render (2048 * 16);
                outDir.getChildFile (String (idx).paddedLeft ('0', 4) + "_" + X + ".f32")
                    .replaceWithData (frames.data(), frames.size() * sizeof (float));
            }
            ++idx;
        }
        std::cout << "captured " << idx << "\n";
        return 0;
    }
    return 2;
}
