// FableSerumImport — converts the user's Serum 1 presets into WT-1 presets
// with their own Serum install. FableSynth launches it as a separate process
// so a Serum crash can never take down the host DAW.
//
//   FableSerumImport [--out DIR] [--source DIR_OR_FXP]... [--frames N]
//
// Defaults: the Serum preset folders of a standard install, and
// ~/Library/Application Support/FableSynth/Imported. One JSON per preset at
// OUT/<category>/<name>.json. Progress on stdout, one line each:
//   TOTAL n | PRESET i name | SKIP name reason | DONE converted skipped | ERROR message
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
#include "ImportedLibrary.h"
#include "SerumHost.h"

using namespace juce;
using namespace serumimport;

static void say(const String& line) { std::cout << line << std::endl; }

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI init;
    File out = importedFolder();
    Array<File> sources;
    int frames = 64;
    for (int i = 1; i < argc; ++i) {
        const String a(argv[i]);
        if (a == "--out" && i + 1 < argc) out = File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
        else if (a == "--source" && i + 1 < argc) sources.add(File::getCurrentWorkingDirectory().getChildFile(argv[++i]));
        else if (a == "--frames" && i + 1 < argc) frames = jlimit(1, 64, String(argv[++i]).getIntValue());
    }
    if (sources.isEmpty()) sources = findSerumPresetRoots();
    const File plugin = findSerumPlugin();
    if (!plugin.exists()) { say("ERROR Serum is not installed (Serum.vst3 not found)"); return 1; }
    if (sources.isEmpty()) { say("ERROR no Serum preset folder found"); return 1; }

    // Collect presets; the folder above each file names its category.
    Array<File> files;
    for (auto& s : sources) {
        if (s.existsAsFile()) files.add(s);
        else for (auto& f : s.findChildFiles(File::findFiles, true, "*.fxp")) files.add(f);
    }
    files.sort();
    say("TOTAL " + String(files.size()));

    SerumHost host;
    String error;
    if (!host.load(plugin, error)) { say("ERROR " + error); return 1; }

    int done = 0, skipped = 0;
    std::vector<ImportedEntry> entries;
    for (int i = 0; i < files.size(); ++i) {
        const auto& f = files.getReference(i);
        const String name = f.getFileNameWithoutExtension();
        say("PRESET " + String(i + 1) + " " + name);
        MemoryBlock data;
        FxpData fxp;
        if (!f.loadFileAsData(data) || !parseFxp(data, fxp, error)) { say("SKIP " + name + " " + error); ++skipped; continue; }
        const auto dump = host.readPreset(fxp);
        Capture cap;
        cap.oscA = host.captureOsc(fxp, 'A', frames);
        cap.oscB = host.captureOsc(fxp, 'B', frames);
        const String category = f.getParentDirectory().getFileName();
        const auto preset = convert(dump, fxp, cap, name.toStdString(), category.toStdString());
        const File dest = out.getChildFile(File::createLegalFileName(category)).getChildFile(File::createLegalFileName(name) + ".json");
        dest.getParentDirectory().createDirectory();
        if (!dest.replaceWithText(JSON::toString(toJson(preset), true))) { say("SKIP " + name + " cannot write " + dest.getFullPathName()); ++skipped; continue; }
        entries.push_back({ name, category, dest, preset.params });
        ++done;
        // Flush the index often, so a cancelled import keeps what it converted.
        if (entries.size() >= 10) {
            if (!updateIndex(out, entries)) { say("ERROR cannot write " + indexFile(out).getFullPathName()); return 1; }
            entries.clear();
        }
    }
    if (!entries.empty() && !updateIndex(out, entries)) { say("ERROR cannot write " + indexFile(out).getFullPathName()); return 1; }
    say("DONE " + String(done) + " " + String(skipped));
    return 0;
}
