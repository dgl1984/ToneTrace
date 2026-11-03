#include <clap/clap.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef TONETRACE_EXPECTED_VERSION
#error TONETRACE_EXPECTED_VERSION must be supplied by CMake from TONETRACE_VERSION
#endif

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
#ifdef _WIN32
using Library = HMODULE;
Library openLibrary(const std::string& path) { return LoadLibraryA(path.c_str()); }
void* findSymbol(Library library, const char* name) { return reinterpret_cast<void*>(GetProcAddress(library, name)); }
void closeLibrary(Library library) { FreeLibrary(library); }
#else
using Library = void*;
// A macOS CLAP is a bundle; its executable shares the bundle's base name.
Library openLibrary(const std::string& path) {
    auto binary = path;
#ifdef __APPLE__
    const auto slash = path.find_last_of('/');
    auto name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    if (const auto dot = name.rfind(".clap"); dot != std::string::npos) name.erase(dot);
    binary = path + "/Contents/MacOS/" + name;
#endif
    return dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* findSymbol(Library library, const char* name) { return dlsym(library, name); }
void closeLibrary(Library library) { dlclose(library); }
#endif

struct HostData { bool audio = false; std::atomic<bool> callback {false}; };
HostData& data(const clap_host_t* h) { return *static_cast<HostData*>(h->host_data); }
bool CLAP_ABI mainThread(const clap_host_t*) { return true; }
bool CLAP_ABI audioThread(const clap_host_t* h) { return data(h).audio; }
const clap_host_thread_check_t threadCheck {mainThread, audioThread};
const void* CLAP_ABI extension(const clap_host_t*, const char* id) {
    return std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0 ? &threadCheck : nullptr;
}
void CLAP_ABI request(const clap_host_t*) {}
void CLAP_ABI callback(const clap_host_t* h) { data(h).callback.store(true); }
uint32_t CLAP_ABI emptySize(const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI emptyGet(const clap_input_events_t*, uint32_t) { return nullptr; }
bool CLAP_ABI push(const clap_output_events_t*, const clap_event_header_t*) { return true; }
const clap_input_events_t noEvents {nullptr, emptySize, emptyGet};
const clap_output_events_t outputEvents {nullptr, push};

struct Instance {
    const clap_plugin_t* p;
    HostData& host;
    bool active = false, processing = false;
    ~Instance() {
        if (processing) { host.audio = true; p->stop_processing(p); host.audio = false; }
        if (active) p->deactivate(p);
        if (p) p->destroy(p);
    }
};

void drain(Instance& i) {
    if (i.host.callback.exchange(false)) i.p->on_main_thread(i.p);
#ifdef _WIN32
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
#endif
}

void parameter(Instance& i, const clap_plugin_params_t* params, clap_id id, double value) {
    clap_event_param_value_t e {};
    e.header.size = sizeof(e); e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_PARAM_VALUE; e.param_id = id; e.value = value;
    e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
    clap_input_events_t input {&e,
        [](const clap_input_events_t*) -> uint32_t { return 1; },
        [](const clap_input_events_t* events, uint32_t index) -> const clap_event_header_t* {
            return index == 0 ? &static_cast<const clap_event_param_value_t*>(events->ctx)->header : nullptr;
        }};
    params->flush(i.p, &input, &outputEvents);
}

struct State { std::vector<char> bytes; size_t position = 0; };
int64_t CLAP_ABI writeState(const clap_ostream_t* s, const void* buffer, uint64_t size) {
    auto& state = *static_cast<State*>(s->ctx);
    const auto n = static_cast<size_t>(std::min<uint64_t>(size, 37));
    const auto* begin = static_cast<const char*>(buffer);
    state.bytes.insert(state.bytes.end(), begin, begin + n); return static_cast<int64_t>(n);
}
int64_t CLAP_ABI readState(const clap_istream_t* s, void* buffer, uint64_t size) {
    auto& state = *static_cast<State*>(s->ctx);
    const auto n = std::min({static_cast<size_t>(size), size_t(29), state.bytes.size() - state.position});
    std::memcpy(buffer, state.bytes.data() + state.position, n); state.position += n;
    return static_cast<int64_t>(n);
}
} // namespace

int main(int argc, char** argv) {
    Library library = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    bool initialized = false;
    try {
        require(argc == 2, "supply the CLAP binary path");
        library = openLibrary(argv[1]);
        require(library != nullptr, "could not open CLAP library");
        entry = reinterpret_cast<const clap_plugin_entry_t*>(findSymbol(library, "clap_entry"));
        require(entry != nullptr, "clap_entry symbol missing");
        require(entry->clap_version.major >= 1, "unsupported CLAP ABI");
        initialized = entry->init(argv[1]);
        require(initialized, "entry init failed");

        const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        require(factory != nullptr, "plugin factory missing");
        require(factory->get_plugin_count(factory) == 1, "expected exactly 1 plug-in");

        const auto* desc = factory->get_plugin_descriptor(factory, 0);
        require(desc != nullptr, "descriptor missing");
        require(std::strcmp(desc->id, "com.lanesaudio.tonetrace-eq") == 0, "wrong plug-in ID");
        require(std::strcmp(desc->name, "Tone Trace EQ") == 0, "wrong plug-in name");
        require(std::strcmp(desc->vendor, "Lanes Audio") == 0, "wrong vendor");
        require(std::strcmp(desc->version, TONETRACE_EXPECTED_VERSION) == 0, "wrong plug-in version");

        HostData hostData {};
        clap_host_t host {
            CLAP_VERSION,
            &hostData,
            "ToneTraceValidationHost",
            "Lanes Audio",
            "https://lanesaudio.com",
            TONETRACE_EXPECTED_VERSION,
            extension,
            request,
            callback
        };

        State saved;
        for (int pass = 0; pass < 12; ++pass) {
            Instance instance {factory->create_plugin(factory, &host, desc->id), hostData};
            require(instance.p != nullptr, "create_plugin failed");
            require(instance.p->init(instance.p), "plugin init failed");
            drain(instance);

            const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(instance.p->get_extension(instance.p, CLAP_EXT_AUDIO_PORTS));
            require(ports && ports->count(instance.p, true) == 1 && ports->count(instance.p, false) == 1, "expected 1 in/out audio port");
            clap_audio_port_info_t inInfo {}, outInfo {};
            require(ports->get(instance.p, 0, true, &inInfo) && inInfo.channel_count == 2, "stereo in missing");
            require(ports->get(instance.p, 0, false, &outInfo) && outInfo.channel_count == 2, "stereo out missing");

            const auto* params = static_cast<const clap_plugin_params_t*>(instance.p->get_extension(instance.p, CLAP_EXT_PARAMS));
            const auto* state = static_cast<const clap_plugin_state_t*>(instance.p->get_extension(instance.p, CLAP_EXT_STATE));
            require(params != nullptr && state != nullptr, "params or state extension missing");

            const auto paramCount = params->count(instance.p);
            require(paramCount > 0, "no parameters advertised");

            clap_id gainId = CLAP_INVALID_ID;
            clap_id strengthId = CLAP_INVALID_ID;
            clap_id bypassId = CLAP_INVALID_ID;

            for (uint32_t i = 0; i < paramCount; ++i) {
                clap_param_info_t info {};
                require(params->get_info(instance.p, i, &info), "param info failed");
                if (std::strcmp(info.name, "Correction Gain") == 0) gainId = info.id;
                if (std::strcmp(info.name, "Correction Strength") == 0) strengthId = info.id;
                if (std::strcmp(info.name, "Bypass") == 0) bypassId = info.id;

                double val = 0.0; char text[256] {};
                require(params->get_value(instance.p, info.id, &val), "get_value failed");
                require(val >= info.min_value && val <= info.max_value, "value outside declared range");
                require(params->value_to_text(instance.p, info.id, val, text, sizeof(text)) && text[0], "value_to_text failed");
            }

            require(gainId != CLAP_INVALID_ID, "Correction Gain parameter missing");
            require(strengthId != CLAP_INVALID_ID, "Correction Strength parameter missing");
            require(bypassId != CLAP_INVALID_ID, "Bypass parameter missing");

            if (pass == 0) {
                parameter(instance, params, gainId, 3.0);
                parameter(instance, params, strengthId, 0.5);
                parameter(instance, params, bypassId, 1.0);
                clap_ostream_t output {&saved, writeState};
                require(state->save(instance.p, &output) && !saved.bytes.empty(), "state save failed");
            } else {
                saved.position = 0; clap_istream_t input {&saved, readState};
                require(state->load(instance.p, &input), "state load failed");
                double val = 0.0;
                require(params->get_value(instance.p, gainId, &val) && std::abs(val - 3.0) < 0.05, "state restore gain mismatch");
                require(params->get_value(instance.p, strengthId, &val) && std::abs(val - 0.5) < 0.05, "state restore strength mismatch");
                require(params->get_value(instance.p, bypassId, &val) && std::abs(val - 1.0) < 0.05, "state restore bypass mismatch");
            }

            const double rate = std::array<double, 3>{44100.0, 48000.0, 96000.0}[pass % 3];
            instance.active = instance.p->activate(instance.p, rate, 1, 512);
            require(instance.active, "CLAP activation failed");
            hostData.audio = true;
            instance.processing = instance.p->start_processing(instance.p);
            require(instance.processing, "CLAP start_processing failed");

            std::array<float, 512> inL {}, inR {}, outL {}, outR {};
            float* inputs[] {inL.data(), inR.data()}; float* outputs[] {outL.data(), outR.data()};
            clap_audio_buffer_t inputBuf {}; inputBuf.data32 = inputs; inputBuf.channel_count = 2;
            clap_audio_buffer_t outputBuf {}; outputBuf.data32 = outputs; outputBuf.channel_count = 2;

            int64_t clock = 0;
            for (uint32_t frames : {1u, 17u, 64u, 511u, 512u, 512u}) {
                inL.fill(0); inR.fill(0); outL.fill(0); outR.fill(0);
                if (clock == 0) inL[0] = inR[0] = 0.5f;
                clap_process_t proc {};
                proc.steady_time = clock; proc.frames_count = frames;
                proc.audio_inputs = &inputBuf; proc.audio_outputs = &outputBuf;
                proc.audio_inputs_count = proc.audio_outputs_count = 1;
                proc.in_events = &noEvents; proc.out_events = &outputEvents;
                require(instance.p->process(instance.p, &proc) != CLAP_PROCESS_ERROR, "CLAP process failed");
                for (uint32_t n = 0; n < frames; ++n) {
                    require(std::isfinite(outL[n]) && std::isfinite(outR[n]), "non-finite audio");
                }
                clock += frames;
            }

            instance.p->stop_processing(instance.p); instance.processing = false; hostData.audio = false;
            instance.p->deactivate(instance.p); instance.active = false; drain(instance);
        }

        entry->deinit(); initialized = false; closeLibrary(library); library = nullptr;
        std::cout << "PASS: CLAP binary metadata, ranges, state with partial I/O, 12 lifecycles, variable blocks, 44.1/48/96 kHz audio\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        if (initialized) entry->deinit();
        if (library) closeLibrary(library);
        return 1;
    }
}
