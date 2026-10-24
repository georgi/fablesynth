# FableVstHost

FableVstHost hosts third-party VST3 instruments for NodeTool's timeline. NodeTool's
desktop app starts it as a child process, so a plug-in that crashes ends the
helper and never the editor. The helper restarts on the next request.

It does two jobs:

- **Offline render.** It plays note events through a plug-in and writes the
  result as planar stereo float32 to a file the caller names. Each render resets
  the plug-in and applies the track's saved state first, so the result depends
  only on that state and the notes.
- **Plug-in window.** It opens the plug-in's own editor in a native window and
  reports each change to the plug-in's state, so the timeline can save it.

The JSON-line protocol is documented at the top of
[`source/vsthost/VstHostMain.cpp`](../source/vsthost/VstHostMain.cpp). The
render, state and scan code is in [`source/vsthost/VstHost.cpp`](../source/vsthost/VstHost.cpp).

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FableVstHost vst_host_test
ctest --test-dir build -R vst_host_test
```

`vst_host_test` loads the WT-1 VST3 the same build produces, renders it, and
round-trips its state. The release workflow attaches `FableVstHost-<platform>.zip`
to each GitHub release.

## Use from NodeTool

Point `NODETOOL_VST_HOST` at the executable, or put it in the desktop app's
`<userData>/vst-host/` folder. NodeTool scans the standard VST3 folders:

| Platform | Folders |
| --- | --- |
| macOS | `~/Library/Audio/Plug-Ins/VST3`, `/Library/Audio/Plug-Ins/VST3` |
| Windows | `C:\Program Files\Common Files\VST3` |
| Linux | `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3` |

On Linux the plug-in window needs a running window manager. Rendering works
without a display.
