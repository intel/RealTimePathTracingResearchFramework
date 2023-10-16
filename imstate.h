// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once
#include "imgui.h"
#include <string>

struct ImGuiContext;
struct ImGuiTextBuffer;

enum struct ImMode : unsigned {
    None           =  0x00,
    GUI            =  0x01,
    NonDefaultMask = ~0x01u,

    SerializationMask  = 0x02,
    Serialize          = 0x02,
    Deserialize        = 0x03,
};

namespace ImState {

bool Open(const char* target = nullptr);

bool Begin(char const* name, void const* object, bool force_open_level = false, bool force_new_object = false);
bool Attribute(char const* name, char* buf, size_t N);
bool Attribute(char const* name, float* values, int N = 1);
bool Attribute(char const* name, int* values, int N = 1);
bool Attribute(char const* name, bool* values, int N = 1); // checkbox-style/automatic interface
bool Attribute(char const* name, bool active); // radiobutton-style/returnval interface
void End(void const* object);

void BeginWrite(ImGuiTextBuffer* output_buffer = nullptr);
void EndWrite();

void BeginRead();
void EndRead();

void RegisterApplicationSettings(ImGuiContext& g);

void SetApplicationIniFile(char const* file);

void ClearSettings(bool autoReload = true);
void SwitchSettings(bool rewind = false);
bool HaveNewSettings(double timecode = 0.0f);
bool NewSettingsSource(std::string& current_settings_source);
void HandledNewSettings();

void AppendFrame(double delay);
void PadFrames(int minNumAfterStart = 1);
int NumKeyframes();
int CurrentKeyframe();
bool LastKeyframeComingUp(double timecode);

bool NeedSettingsUpdate();
bool UpdateSettings();
bool LoadSettings(char const* file = nullptr);
bool WriteSettings(char const* file = nullptr);

extern ImMode CurrentMode;
inline ImMode GetCurrentMode() {
    return ((int) CurrentMode & (int) ImMode::NonDefaultMask) ? CurrentMode : ImMode::GUI;
}
inline bool InDefaultMode() {
    return !((int) CurrentMode & (int) ImMode::NonDefaultMask);
}
inline bool InReadMode() {
    return CurrentMode == ImMode::Deserialize;
}

struct SettingsHandler {
    int phase = 0;
    ImMode mode = ImMode::None;

    SettingsHandler() = default;
    SettingsHandler(SettingsHandler const&) = delete;
    SettingsHandler& operator=(SettingsHandler const&) = delete;

    ~SettingsHandler() {
        // finish open phase
        if (phase & 0x1)
            next();
    }

    bool next(float timecode = 0.0f) {
        while (true) {
            switch (phase++) {
            case 0:
                if (HaveNewSettings(timecode)) {
                    BeginRead();
                    mode = ImMode::Deserialize;
                    return true;
                }
                else
                    break;
            case 1:
                mode = ImMode::None;
                EndRead();
                HandledNewSettings();
                continue;
            case 2:
                if (NeedSettingsUpdate()) {
                    BeginWrite();
                    mode = ImMode::Serialize;
                    return true;
                }
                else
                    break;
            case 3:
                mode = ImMode::None;
                EndWrite();
                UpdateSettings();
                continue;
            default:
                return false;
            }
            ++phase; // skip end of phase
        }
    }
};

struct SettingsWriter {
    char const* to_file = nullptr;
    ImMode mode = ImMode::None;

    SettingsWriter() = default;
    SettingsWriter(SettingsWriter const&) = delete;
    SettingsWriter& operator=(SettingsWriter const&) = delete;

    ~SettingsWriter() {
        // finish open phase
        if (mode == ImMode::Serialize)
            next();
    }
    bool next() {
        if (mode == ImMode::None) {
            BeginWrite();
            mode = ImMode::Serialize;
            return true;
        }
        else if (mode == ImMode::Serialize) {
            mode = ImMode::Deserialize; // mark end of serialization by reverse
            EndWrite();
            WriteSettings(to_file);
        }
        return false;
    }
};

} // namespace

#define IMGUI_LIST_BEGIN(f, n, o, fill_count, ...) (ImState::InDefaultMode() ? (*(fill_count) = -1, f(n, ##__VA_ARGS__)) : (bool) (fill_count))
#define IMGUI_LIST_END(f, o, ...) (ImState::InDefaultMode() ? f(__VA_ARGS__) : (void) 0)

#define IMGUI_STATE_BEGIN_ALWAYS(f, n, o, ...) (ImState::InDefaultMode() ? f(n, ##__VA_ARGS__) : ImState::Begin(n, o, true))
#define IMGUI_STATE_BEGIN(f, n, o, ...) (ImState::InDefaultMode() ? f(n, ##__VA_ARGS__) : ImState::Begin(n, o))
#define IMGUI_STATE_BEGIN_ATOMIC_COMBO(f, n, o, ...) (ImState::InDefaultMode() ? f(n, ##__VA_ARGS__) : ImState::Begin(n, o, false, true))
#define IMGUI_STATE_END(f, o, ...) (ImState::InDefaultMode() ? f(__VA_ARGS__) : ImState::End(o))

#define IMGUI_STATE_BEGIN_HEADER(f, n, o, ...) (ImState::InDefaultMode() ? f(n, ##__VA_ARGS__) : ImState::Begin(n, o))
#define IMGUI_STATE_END_HEADER(o, ...) (ImState::InDefaultMode() ? (void) 0 : ImState::End(o))

#define IMGUI_STATE_ACTION( f, n, ...) (ImState::InDefaultMode() ? f(n, ##__VA_ARGS__) : ImState::Attribute(n, false))

#define IMGUI_STATE( f, n, v, ...)    (ImState::InDefaultMode() ?    f(n, v, ##__VA_ARGS__) : ImState::Attribute(n, v   ))
#define IMGUI_STATE1(f, n, v, ...)    (ImState::InDefaultMode() ?    f(n, v, ##__VA_ARGS__) : ImState::Attribute(n, v   ))
#define IMGUI_STATE2(f, n, v, ...)    (ImState::InDefaultMode() ? f##2(n, v, ##__VA_ARGS__) : ImState::Attribute(n, v, 2))
#define IMGUI_STATE3(f, n, v, ...)    (ImState::InDefaultMode() ? f##3(n, v, ##__VA_ARGS__) : ImState::Attribute(n, v, 3))
#define IMGUI_STATE4(f, n, v, ...)    (ImState::InDefaultMode() ? f##4(n, v, ##__VA_ARGS__) : ImState::Attribute(n, v, 4))
#define IMGUI_STATE_(f, n, v, N, ...) (ImState::InDefaultMode() ? f(n, v, N, ##__VA_ARGS__) : ImState::Attribute(n, v, N))

#define IMGUI_OFFER( f, n, v, ...)    (ImState::InDefaultMode() ?    f(n, v, ##__VA_ARGS__) : ImState::InReadMode() && ImState::Attribute(n, v   ))
#define IMGUI_OFFER1(f, n, v, ...)    (ImState::InDefaultMode() ?    f(n, v, ##__VA_ARGS__) : ImState::InReadMode() && ImState::Attribute(n, v   ))
#define IMGUI_OFFER2(f, n, v, ...)    (ImState::InDefaultMode() ? f##2(n, v, ##__VA_ARGS__) : ImState::InReadMode() && ImState::Attribute(n, v, 2))
#define IMGUI_OFFER3(f, n, v, ...)    (ImState::InDefaultMode() ? f##3(n, v, ##__VA_ARGS__) : ImState::InReadMode() && ImState::Attribute(n, v, 3))
#define IMGUI_OFFER4(f, n, v, ...)    (ImState::InDefaultMode() ? f##4(n, v, ##__VA_ARGS__) : ImState::InReadMode() && ImState::Attribute(n, v, 4))
#define IMGUI_OFFER_(f, n, v, N, ...) (ImState::InDefaultMode() ? f(n, v, N, ##__VA_ARGS__) : ImState::InReadMode() && ImState::Attribute(n, v, N))

#define IMGUI_VOLATILE_HEADER(f, n, ...) (ImState::InDefaultMode() ? f(n, ##__VA_ARGS__) : true)
#define IMGUI_VOLATILE(...)              (ImState::InDefaultMode() ? (void) (__VA_ARGS__) : (void) 0)
#define IMGUI_NO_UI(...)                 (false)
