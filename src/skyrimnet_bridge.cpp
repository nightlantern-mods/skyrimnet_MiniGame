#include "skyrimnet_bridge.h"

#include "settings.h"
#include "skyrimnet/PublicAPI.h"

#include <chrono>
#include <mutex>
#include <set>
#include <thread>

#include <nlohmann/json.hpp>

namespace SkyrimNetBridge
{
    namespace
    {
        constexpr const char* kBusyReason = "minigame";

        std::mutex                                 g_lock;
        std::unordered_map<RE::FormID, std::string> g_states;  // what each NPC's prompts see of the game
        std::set<RE::FormID>                       g_busy;    // marked busy in SkyrimNet while playing
        std::chrono::steady_clock::time_point      g_lastNarration{};

        bool g_ready = false;

        void SetBusy(RE::FormID a_actor, bool a_busy)
        {
            if (a_busy && PublicSetActorBusy) {
                PublicSetActorBusy(a_actor, kBusyReason);
            } else if (!a_busy && PublicClearActorBusy) {
                PublicClearActorBusy(a_actor);
            }
        }

        // The line of a "dialogue" event: its data is JSON text ({"text": ...} or {"dialogue": ...})
        // or the line itself.
        std::string DialogueLine(const nlohmann::json& a_event)
        {
            auto data = a_event.contains("data") ? a_event["data"] : nlohmann::json();
            if (data.is_string()) {
                auto parsed = nlohmann::json::parse(data.get<std::string>(), nullptr, false);
                if (!parsed.is_object()) {
                    return data.get<std::string>();
                }
                data = std::move(parsed);
            }
            if (data.is_object()) {
                for (const char* key : { "text", "dialogue", "line" }) {
                    if (data.contains(key) && data[key].is_string()) {
                        return data[key].get<std::string>();
                    }
                }
            }
            return {};
        }
    }

    void Init(std::function<bool(RE::Actor*)> a_canChallenge, std::function<void(RE::FormID)> a_onChallenge,
        std::function<void(RE::FormID, std::string)> a_onDialogue)
    {
        if (!FindFunctions() || !PublicGetVersion) {
            logger::warn("SkyrimNet API not found; the games work without commentary");
            return;
        }
        g_ready = true;
        logger::info("SkyrimNet API v{}", PublicGetVersion());

        if (!PublicRegisterEvent) {
            logger::warn("This SkyrimNet cannot register events (needs API v11+); no commentary");
        }

        // {{ minigame_state(actorUUID) }}: the game this actor is playing with the player, or "".
        if (PublicRegisterDecorator) {
            PublicRegisterDecorator("minigame_state", "The mini game (Reversi, Liar's Dice...) this actor is playing with the player right now (empty if none)",
                [](RE::Actor* a_actor) -> std::string {
                    if (!a_actor) {
                        return {};
                    }
                    std::scoped_lock lock(g_lock);
                    const auto       it = g_states.find(a_actor->GetFormID());
                    return it != g_states.end() ? it->second : std::string();
                });
        }

        // Paced games wait for each line to be spoken before they go on.
        if (PublicRegisterEventCallback) {
            const auto onLine = [a_onDialogue](const char* a_json) {
                const auto event = nlohmann::json::parse(a_json ? a_json : "", nullptr, false);
                if (!event.is_object() || !event.contains("originatingActorFormId") || !event["originatingActorFormId"].is_number()) {
                    return;
                }
                const auto speaker = event["originatingActorFormId"].get<RE::FormID>();
                if (speaker == 0x14) {
                    logger::info("player line ({}): {}", event.value("type", std::string()), DialogueLine(event));
                }
                a_onDialogue(speaker, DialogueLine(event));
            };
            // the player's lines may come as either
            PublicRegisterEventCallback("dialogue", onLine);
            PublicRegisterEventCallback("dialogue_player_text", onLine);
        } else {
            logger::warn("This SkyrimNet cannot report spoken lines; paced games wait out the MCM voice wait instead");
        }

        if (PublicRegisterCPPAction) {
            const bool registered = PublicRegisterCPPAction("Reversi_Challenge",
                "Challenge the player to a game of Reversi (Othello) on a small board you carry. Choose this only when you would "
                "genuinely like to play a board game with them right now: they asked for a game, you are idle or relaxing together, "
                "or you want a friendly contest. Not during danger, urgent business or a serious conversation.",
                [a_canChallenge](RE::Actor* a_actor) { return a_actor && a_canChallenge(a_actor); },
                [a_onChallenge](RE::Actor* a_actor, std::string) {
                    if (!a_actor) {
                        return false;
                    }
                    a_onChallenge(a_actor->GetFormID());
                    return true;
                },
                "dialogue", "Social", 50, "{}", "", "", "");
            logger::info("Reversi_Challenge action {}", registered ? "registered" : "NOT registered");
        }
    }

    bool CanNarrate()
    {
        return g_ready && PublicRegisterEvent && Settings::Get().commentary;
    }

    void Narrate(RE::FormID a_speaker, std::string a_text, Level a_level)
    {
        if (!CanNarrate() || !a_speaker || a_text.empty()) {
            return;
        }
        {
            std::scoped_lock lock(g_lock);
            const auto now = std::chrono::steady_clock::now();
            const auto wait = std::chrono::seconds(Settings::Get().cooldownSeconds) / (a_level == Level::kNotable ? 2 : 1);
            if (a_level != Level::kImportant && now - g_lastNarration < wait) {
                return;
            }
            g_lastNarration = now;
        }
        // On the game thread: SkyrimNet checks who can see the speaker with physics raycasts,
        // which fail on other threads (and the event then reaches nobody).
        SKSE::GetTaskInterface()->AddTask([a_speaker, text = std::move(a_text)]() {
            const auto id = PublicRegisterEvent("direct_narration", text.c_str(), a_speaker,
                RE::PlayerCharacter::GetSingleton()->GetFormID(), nullptr, 0);
            logger::info("narration {} ({:08X}): {}", id, a_speaker, text);
        });
    }

    bool CanPlayerSay()
    {
        return CanNarrate() && Settings::Get().playerVoice;
    }

    void PlayerSay(std::string a_text)
    {
        if (!CanPlayerSay() || a_text.empty()) {
            return;
        }
        SKSE::GetTaskInterface()->AddTask([text = std::move(a_text)]() {
            const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
            // the VM owns the arguments
            const bool sent = vm && vm->DispatchStaticCall("SkyrimNetApi", "TransformDialogue", RE::MakeFunctionArguments(std::string(text)), none);
            logger::info("player says{}: {}", sent ? "" : " (SkyrimNetApi.TransformDialogue not found)", text);
        });
    }

    void SetStates(std::unordered_map<RE::FormID, std::string> a_states, bool a_playing)
    {
        std::erase_if(a_states, [](const auto& a_entry) { return !a_entry.first || a_entry.second.empty(); });
        std::set<RE::FormID> busy;
        if (a_playing) {
            for (const auto& [actor, text] : a_states) {
                busy.insert(actor);
            }
        }
        std::vector<RE::FormID> released;
        std::vector<RE::FormID> engaged;
        {
            std::scoped_lock lock(g_lock);
            g_states = std::move(a_states);
            std::ranges::set_difference(g_busy, busy, std::back_inserter(released));
            std::ranges::set_difference(busy, g_busy, std::back_inserter(engaged));
            g_busy = std::move(busy);
        }
        if (g_ready) {
            for (const auto actor : released) {
                SetBusy(actor, false);
            }
            for (const auto actor : engaged) {
                SetBusy(actor, true);
            }
        }
    }

    void Reset()
    {
        SetStates({}, false);
    }
}
