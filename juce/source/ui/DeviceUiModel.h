#pragma once

#include "ParameterSource.h"

// Host transport snapshot used by tempo-synchronised device displays.
struct HostTransport {
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
};

namespace fui {

struct DeviceUiCapabilities {
    bool hosted = false;
    bool ownsTransport = true;
    bool supportsPatternChain = true;
    bool supportsUserTables = true;
    bool supportsAudition = true;
};

// Common contract shared by the three native device bodies. Implementations
// are message-thread objects and must outlive the components that reference
// them. Audio-thread state is exposed only through atomics owned by processors.
class DeviceUiModel {
public:
    virtual ~DeviceUiModel() = default;
    virtual ParameterSource parameters() = 0;
    virtual DeviceUiCapabilities capabilities() const = 0;
};

} // namespace fui
