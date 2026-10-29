#pragma once

// MCM settings. MCM Helper writes the user's values to Data/MCM/Settings/skyrimnet_MiniGame.ini;
// the defaults live in Data/MCM/Config/skyrimnet_MiniGame/settings.ini.
namespace Settings
{
    struct Values
    {
        int  difficulty = 2;  // 1 beginner .. 4 master
        int  color = 0;       // 0 black, 1 white, 2 random, 3 alternate
        bool hints = true;
        bool pauseGame = false;
        int  scanRadius = 1500;  // game units: how far away an NPC may be to be listed as an opponent
        bool gather = true;      // a new game calls its opponents to stand in front of the player
        int  diceDifficulty = 2;  // Liar's Dice: 1 beginner .. 4 master
        bool commentary = true;
        int  cooldownSeconds = 8;
        int  voiceWaitSeconds = 15;  // a paced game moves on when the speaker stays silent this long
        bool playerVoice = true;     // Liar's Dice: the player's bids go through SkyrimNet's Transform Dialogue
        bool challenge = true;
    };

    // Re-reads both files. Call from any thread.
    void Reload();

    Values Get();
}
