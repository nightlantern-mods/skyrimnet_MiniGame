#include "skyrimnet_bridge.h"

#include "settings.h"
#include "skyrimnet/PublicAPI.h"

#include <array>
#include <chrono>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

namespace SkyrimNetBridge
{
    namespace
    {
        constexpr const char* kBusyReason = "minigame";

        std::mutex                                 g_lock;
        std::unordered_map<RE::FormID, std::string> g_states;  // what each NPC's prompts see of the game
        // minigame_style, by the MCM's iLineLength (2: no instruction)
        std::array<std::string, 4> g_lineStyles = {
            "Remarks about the game are a single short line: a few words, one sentence at most.",
            "Remarks about the game stay short, like table talk: one or two sentences.",
            "",
            "Remarks about the game may run longer: two to four sentences, with what they feel and see at the table.",
        };
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
            PublicRegisterDecorator("minigame_state", "The mini game (Reversi, Liar's Dice, Shut the Box, Blackjack, Texas Hold'em...) this actor is playing with the player right now (empty if none)",
                [](RE::Actor* a_actor) -> std::string {
                    if (!a_actor) {
                        return {};
                    }
                    std::scoped_lock lock(g_lock);
                    const auto       it = g_states.find(a_actor->GetFormID());
                    return it != g_states.end() ? it->second : std::string();
                });
            // {{ minigame_style(actorUUID) }}: how long this actor's lines about the game should be
            // (MCM), while they are at the table; "" otherwise and for "SkyrimNet's own".
            PublicRegisterDecorator("minigame_style", "How long this actor's remarks about the mini game they are playing should be, as the player set it (empty if none)",
                [](RE::Actor* a_actor) -> std::string {
                    if (!a_actor) {
                        return {};
                    }
                    const auto       length = static_cast<std::size_t>(Settings::Get().lineLength);
                    std::scoped_lock lock(g_lock);
                    return g_states.contains(a_actor->GetFormID()) ? g_lineStyles[length] : std::string();
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

    namespace
    {
        constexpr const char* kPlayerLinePrompt = "skyrimnet_minigame_player_line";

        // The LLM's line without wrapping quotes, line breaks or a runaway length.
        std::string CleanLine(std::string a_line)
        {
            for (auto& c : a_line) {
                if (c == '\r' || c == '\n') {
                    c = ' ';
                }
            }
            const auto first = a_line.find_first_not_of(" \t\"'");
            const auto last = a_line.find_last_not_of(" \t\"'");
            a_line = first == std::string::npos ? std::string() : a_line.substr(first, last - first + 1);
            return a_line.size() > 600 ? a_line.substr(0, 600) : a_line;
        }

        // The player's character says a_line (their voice, if SkyrimNet voices the player), and
        // a_listener is told so and answers: a direct narration, like every moment of a game, so
        // nobody else takes the reply.
        void DeliverChat(std::string a_line, RE::FormID a_listener, std::string a_frame)
        {
            SKSE::GetTaskInterface()->AddTask([line = std::move(a_line), a_listener, frame = std::move(a_frame)]() {
                const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
                const bool voiced = vm && vm->DispatchStaticCall("SkyrimNetApi", "TriggerPlayerTTS", RE::MakeFunctionArguments(std::string(line)), none);
                auto       told = frame;
                if (const auto at = told.find("{text}"); at != std::string::npos) {
                    told.replace(at, 6, line);
                } else {
                    told += " " + line;
                }
                const auto id = PublicRegisterEvent ? PublicRegisterEvent("direct_narration", told.c_str(), a_listener, RE::PlayerCharacter::GetSingleton()->GetFormID(), nullptr, 0) : 0;
                logger::info("player chat to {:08X} (narration {}, player voice {}): {}", a_listener, id, voiced ? "asked" : "not available", told);
            });
        }
    }

    void PlayerChat(std::string a_text, RE::FormID a_listener, std::string a_listenerName, std::string a_playerName, std::string a_frame, bool a_asWritten,
        std::function<void(std::string)> a_said)
    {
        if (!CanNarrate() || a_text.empty() || !a_listener) {
            return;
        }
        if (a_asWritten || !PublicSendCustomPromptToLLM) {
            if (!a_asWritten && a_said) {
                a_said(a_text);
            }
            DeliverChat(std::move(a_text), a_listener, std::move(a_frame));
            return;
        }
        const nlohmann::json context = {
            { "player_formid", 0x14 },
            { "player_name", a_playerName.empty() ? std::string("the player") : a_playerName },
            { "listener_name", a_listenerName },
            { "text", a_text },
        };
        const bool queued = PublicSendCustomPromptToLLM(kPlayerLinePrompt, "",
            context.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str(),
            [a_text, a_listener, a_frame, a_said](const char* a_response, int a_success) {
                auto line = a_success ? CleanLine(a_response ? a_response : "") : std::string();
                if (line.empty()) {
                    logger::info("player chat: no line in the character's words ({}); said as written", a_response ? std::string(a_response).substr(0, 200) : "");
                    if (a_said) {
                        a_said(a_text);
                    }
                    DeliverChat(a_text, a_listener, a_frame);
                    return;
                }
                if (a_said) {
                    a_said(line);
                }
                DeliverChat(std::move(line), a_listener, a_frame);
            });
        if (!queued) {
            logger::info("player chat: the request was not queued; said as written");
            if (a_said) {
                a_said(a_text);
            }
            DeliverChat(std::move(a_text), a_listener, std::move(a_frame));
        }
    }

    void SetLineStyles(std::string a_brief, std::string a_short, std::string a_long)
    {
        std::scoped_lock lock(g_lock);
        if (!a_brief.empty()) g_lineStyles[0] = std::move(a_brief);
        if (!a_short.empty()) g_lineStyles[1] = std::move(a_short);
        if (!a_long.empty()) g_lineStyles[3] = std::move(a_long);
    }

    bool CanPlayerSay()
    {
        return CanNarrate() && Settings::Get().playerVoice;
    }

    namespace
    {
        std::string CleanLine(std::string a_line);
    }

    void PlayerSay(std::string a_text, std::string a_playerName, std::function<void(std::string)> a_said)
    {
        if (!CanPlayerSay() || a_text.empty()) {
            return;
        }
        const auto deliver = [a_said](std::string a_line) {
            if (a_said) {
                a_said(a_line);
            }
            SKSE::GetTaskInterface()->AddTask([line = std::move(a_line)]() {
                const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
                // the VM owns the arguments
                const bool voiced = vm && vm->DispatchStaticCall("SkyrimNetApi", "TriggerPlayerTTS", RE::MakeFunctionArguments(std::string(line)), none);
                logger::info("player says (player voice {}): {}", voiced ? "asked" : "not available", line);
            });
        };
        if (!PublicSendCustomPromptToLLM) {
            deliver(std::move(a_text));
            return;
        }
        const nlohmann::json context = {
            { "player_formid", 0x14 },
            { "player_name", a_playerName.empty() ? std::string("the player") : a_playerName },
            { "listener_name", "the other players at the table" },
            { "text", a_text },
        };
        const bool queued = PublicSendCustomPromptToLLM("skyrimnet_minigame_player_line", "",
            context.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str(),
            [a_text, deliver](const char* a_response, int a_success) {
                auto line = a_success ? CleanLine(a_response ? a_response : "") : std::string();
                deliver(line.empty() ? a_text : std::move(line));
            });
        if (!queued) {
            deliver(std::move(a_text));
        }
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

    namespace
    {
        constexpr const char* kPokerStylePrompt = "skyrimnet_minigame_poker_style";

        // "HANDS: tight" / "BETS: bold" / "BLUFF: often", one per line, in any order and case.
        nlohmann::json ParsePokerStyle(const std::string& a_text)
        {
            static const std::unordered_map<std::string, std::pair<const char*, std::vector<std::string>>> kKeys = {
                { "HANDS", { "hands", { "tight", "normal", "loose" } } },
                { "BETS", { "bets", { "cautious", "normal", "bold" } } },
                { "BLUFF", { "bluff", { "rarely", "sometimes", "often" } } },
            };
            auto style = nlohmann::json::object();
            std::istringstream lines(a_text);
            std::string        line;
            while (std::getline(lines, line)) {
                const auto colon = line.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                std::string key, value;
                for (const char c : line.substr(0, colon)) {
                    if (std::isalpha(static_cast<unsigned char>(c))) key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                for (const char c : line.substr(colon + 1)) {
                    if (std::isalpha(static_cast<unsigned char>(c))) value += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    else if (!value.empty()) break;
                }
                const auto it = kKeys.find(key);
                if (it != kKeys.end() && std::ranges::find(it->second.second, value) != it->second.second.end()) {
                    style[it->second.first] = value;
                }
            }
            return style.size() == 3 ? style : nlohmann::json();
        }
    }

    void AskPokerStyles(std::vector<std::pair<RE::FormID, std::string>> a_npcs, std::function<void(std::string)> a_reply)
    {
        for (const auto& [actor, name] : a_npcs) {
            const auto id = std::format("{:08X}", actor);
            const auto failed = [a_reply, id](std::string_view a_why) {
                logger::info("poker style of {}: {}", id, a_why);
                a_reply(nlohmann::json{ { "id", id }, { "failed", true } }.dump());
            };
            if (!g_ready || !PublicSendCustomPromptToLLM) {
                failed("SkyrimNet custom prompts are not available");
                continue;
            }
            const nlohmann::json context = { { "npc_formid", actor }, { "npc_name", name } };
            const bool queued = PublicSendCustomPromptToLLM(kPokerStylePrompt, "",
                context.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str(),
                [a_reply, id, failed](const char* a_response, int a_success) {
                    const std::string text = a_response ? a_response : "";
                    if (!a_success) {
                        failed("the request failed: " + text.substr(0, 200));
                        return;
                    }
                    auto style = ParsePokerStyle(text);
                    if (style.is_null()) {
                        failed("no style in the answer: " + text.substr(0, 200));
                        return;
                    }
                    style["id"] = id;
                    logger::info("poker style of {}: {}", id, style.dump());
                    a_reply(style.dump());
                });
            if (!queued) {
                failed("the request was not queued");
            }
        }
    }
}
