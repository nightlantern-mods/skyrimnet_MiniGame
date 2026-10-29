#pragma once

#include <functional>
#include <string>
#include <unordered_map>

// Everything that talks to SkyrimNet. Every function is a no-op when SkyrimNet is missing.
namespace SkyrimNetBridge
{
    // Resolves the API and registers the minigame_state decorator, the challenge action and a
    // listener for spoken lines. a_canChallenge (thread-safe) says whether an NPC may challenge
    // the player now; a_onChallenge runs when one does; a_onDialogue(speaker, line) runs on
    // SkyrimNet's threads whenever an NPC or the player speaks. Call at kDataLoaded.
    void Init(std::function<bool(RE::Actor*)> a_canChallenge, std::function<void(RE::FormID)> a_onChallenge,
        std::function<void(RE::FormID, std::string)> a_onDialogue);

    // Whether Narrate reaches SkyrimNet at all (it is there and commentary is on in the MCM).
    bool CanNarrate();

    enum class Level
    {
        kMinor,      // an ordinary move: dropped within the MCM cooldown of the last remark
        kNotable,    // a corner, a comeback...: dropped within half of it
        kImportant,  // start, end, resign, every turn of a paced game: always sent
    };

    // A moment of the game, as direct narration from a_speaker to the player: they react to it.
    void Narrate(RE::FormID a_speaker, std::string a_text, Level a_level);

    // Whether PlayerSay can work: narration is on and the MCM lets the player speak.
    bool CanPlayerSay();

    // The player says a_text in their own words (SkyrimNet's Transform Dialogue, by Papyrus).
    void PlayerSay(std::string a_text);

    // The game as each NPC's prompts see it (minigame_state; an NPC left out sees nothing), and
    // whether it is still being played (those NPCs are marked busy in SkyrimNet while it is).
    // Replaces what the last call set.
    void SetStates(std::unordered_map<RE::FormID, std::string> a_states, bool a_playing);

    // Forgets every game (a save was loaded).
    void Reset();
}
