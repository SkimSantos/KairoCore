#include "linux_sdl/sdl_app.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include "platform/input_mapper.hpp"
#include "platform/settings_store.hpp"

namespace kairo::linux_sdl {

namespace {
constexpr int kInitialWindowScale = 3;
constexpr int kInitialWindowWidth = kairo::core::VideoFrame::width * kInitialWindowScale;
constexpr int kInitialWindowHeight = kairo::core::VideoFrame::height * kInitialWindowScale;
constexpr float kVolumeStep = 0.1f;

void print_debug_snapshot(const kairo::core::DebugSnapshot& snap) {
    SDL_Log("--- debug snapshot (frame %llu) ---",
            static_cast<unsigned long long>(snap.frame_number));
    SDL_Log("CPU:");
    for (const auto& e : snap.cpu_registers) {
        SDL_Log("  %-6s = %s", e.name.c_str(), e.value.c_str());
    }
    SDL_Log("HW:");
    for (const auto& e : snap.hardware_registers) {
        SDL_Log("  %-8s = %s", e.name.c_str(), e.value.c_str());
    }
    if (!snap.memory_watch.empty()) {
        SDL_Log("Watches:");
        for (const auto& e : snap.memory_watch) {
            SDL_Log("  %s = %s", e.name.c_str(), e.value.c_str());
        }
    }
}
} // namespace

SdlApp::SdlApp() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    }

    window_ = SDL_CreateWindow(
        "KairoCore",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kInitialWindowWidth, kInitialWindowHeight,
        SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    }

    renderer_ = std::make_unique<SdlRenderer>(window_);
    audio_ = std::make_unique<SdlAudio>();
    input_ = std::make_unique<SdlInput>();
    emulator_ = std::make_unique<kairo::core::EmulatorInstance>(
        renderer_.get(), audio_.get());
}

SdlApp::~SdlApp() {
    emulator_.reset();
    input_.reset();
    audio_.reset();
    renderer_.reset();
    if (window_) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

int SdlApp::run(const std::string& rom_path) {
    if (!rom_path.empty()) {
        if (emulator_->load_rom(rom_path)) {
            SDL_Log("ROM loaded: %s", rom_path.c_str());
        } else {
            SDL_Log("Failed to load ROM: %s", rom_path.c_str());
        }
    } else {
        SDL_Log("No ROM path provided");
    }

    // Phase 4: try restoring per-game profile and persisted save slots.
    // Both are best-effort — a missing profile or save directory is fine
    // on first launch, and the frontend should fall back to defaults.
    const auto rom_id = emulator_->rom_id();
    if (rom_id != 0) {
        const auto profile_path = kairo::platform::profile_path_for_rom(rom_id);
        if (const auto profile = kairo::platform::load_profile(profile_path)) {
            emulator_->set_volume(profile->volume);
            emulator_->set_fast_forward_multiplier(profile->fast_forward_multiplier);
            for (int i = 0; i < 10; ++i) {
                const int sc = profile->input_bindings[static_cast<std::size_t>(i)];
                if (sc >= 0) {
                    input_->mapper().set_binding(
                        static_cast<kairo::platform::InputAction>(i), sc);
                }
            }
            SDL_Log("profile: loaded for rom %016llx",
                    static_cast<unsigned long long>(rom_id));
        }

        const auto save_dir = kairo::platform::save_dir_for_rom(rom_id);
        if (emulator_->load_slots_from_dir(save_dir)) {
            SDL_Log("save slots: restored from %s", save_dir.c_str());
        }
    }

    emulator_->run();

    using clock = std::chrono::steady_clock;
    auto next_frame_deadline = clock::now();
    float pre_mute_volume = 1.0f;

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                quit = true;
                continue;
            }
            if (ev.type != SDL_KEYDOWN) continue;

            const SDL_Keycode k = ev.key.keysym.sym;
            switch (k) {
                case SDLK_ESCAPE:
                    quit = true;
                    break;
                case SDLK_p:
                    if (emulator_->is_running()) emulator_->pause();
                    else emulator_->run();
                    break;
                case SDLK_l:
                    emulator_->set_fast_forward_multiplier(
                        emulator_->get_fast_forward_multiplier() + 1);
                    SDL_Log("fast-forward: %dx",
                            emulator_->get_fast_forward_multiplier());
                    break;
                case SDLK_k:
                    emulator_->set_fast_forward_multiplier(
                        emulator_->get_fast_forward_multiplier() - 1);
                    SDL_Log("fast-forward: %dx",
                            emulator_->get_fast_forward_multiplier());
                    break;
                case SDLK_f:
                    emulator_->set_frame_step_mode(!emulator_->is_frame_step_mode());
                    SDL_Log("frame-step mode: %s",
                            emulator_->is_frame_step_mode() ? "on" : "off");
                    break;
                case SDLK_n:
                    if (emulator_->is_frame_step_mode()) {
                        emulator_->submit_input_for_next_frame(input_->poll());
                        emulator_->step_one_frame();
                        // Per spec, frame-step + debug mode auto-dumps
                        // after each advance so the user always sees the
                        // state that produced the current frame.
                        if (emulator_->is_debug_mode()) {
                            print_debug_snapshot(emulator_->capture_debug_snapshot());
                        }
                    }
                    break;
                case SDLK_m:
                    emulator_->set_debug_mode(!emulator_->is_debug_mode());
                    SDL_Log("debug mode: %s",
                            emulator_->is_debug_mode() ? "on" : "off");
                    break;
                case SDLK_b:
                    print_debug_snapshot(emulator_->capture_debug_snapshot());
                    break;
                case SDLK_F5:
                    emulator_->save_state_to_slot(1);
                    SDL_Log("saved slot 1 @ frame %llu",
                            static_cast<unsigned long long>(emulator_->frame_number()));
                    break;
                case SDLK_F6:
                    emulator_->save_state_to_slot(2);
                    SDL_Log("saved slot 2 @ frame %llu",
                            static_cast<unsigned long long>(emulator_->frame_number()));
                    break;
                case SDLK_F7:
                    SDL_Log("load slot 1: %s",
                            emulator_->load_state_from_slot(1) ? "ok" : "empty/invalid");
                    break;
                case SDLK_F8:
                    SDL_Log("load slot 2: %s",
                            emulator_->load_state_from_slot(2) ? "ok" : "empty/invalid");
                    break;
                case SDLK_EQUALS:
                case SDLK_KP_PLUS:
                    emulator_->set_volume(
                        std::min(1.0f, emulator_->get_volume() + kVolumeStep));
                    SDL_Log("volume: %d%%",
                            static_cast<int>(emulator_->get_volume() * 100.0f));
                    break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:
                    emulator_->set_volume(
                        std::max(0.0f, emulator_->get_volume() - kVolumeStep));
                    SDL_Log("volume: %d%%",
                            static_cast<int>(emulator_->get_volume() * 100.0f));
                    break;
                case SDLK_v: {
                    const float current = emulator_->get_volume();
                    if (current > 0.0f) {
                        pre_mute_volume = current;
                        emulator_->set_volume(0.0f);
                        SDL_Log("muted");
                    } else {
                        emulator_->set_volume(pre_mute_volume);
                        SDL_Log("unmuted: %d%%",
                                static_cast<int>(pre_mute_volume * 100.0f));
                    }
                    break;
                }
                default:
                    break;
            }
        }

        emulator_->set_input_state(input_->poll());

        if (emulator_->wants_frame_advance()) {
            emulator_->step_one_frame();
        }

        // Pace from the core's target frame duration so fast-forward
        // changes take effect within one tick. Clamp the deadline to
        // avoid spinning to catch up after a long stall.
        const auto frame_period = emulator_->target_frame_duration();
        const auto now = clock::now();
        if (next_frame_deadline > now + frame_period) {
            next_frame_deadline = now + frame_period;
        }
        if (next_frame_deadline > now) {
            std::this_thread::sleep_until(next_frame_deadline);
            next_frame_deadline += frame_period;
        } else {
            next_frame_deadline = now + frame_period;
        }
    }

    // Persist profile + slots on exit (best-effort; any failure is logged
    // but does not block shutdown).
    if (const auto rid = emulator_->rom_id(); rid != 0) {
        kairo::platform::GameProfile profile;
        profile.volume = emulator_->get_volume();
        profile.fast_forward_multiplier = emulator_->get_fast_forward_multiplier();
        for (int i = 0; i < 10; ++i) {
            profile.input_bindings[static_cast<std::size_t>(i)] =
                input_->mapper().get_binding(
                    static_cast<kairo::platform::InputAction>(i));
        }
        const auto profile_path = kairo::platform::profile_path_for_rom(rid);
        if (!kairo::platform::save_profile(profile_path, profile)) {
            SDL_Log("profile: failed to save to %s", profile_path.c_str());
        }

        const auto save_dir = kairo::platform::save_dir_for_rom(rid);
        if (!emulator_->sync_slots_to_dir(save_dir)) {
            SDL_Log("save slots: failed to sync to %s", save_dir.c_str());
        }
    }

    return 0;
}

} // namespace kairo::linux_sdl
