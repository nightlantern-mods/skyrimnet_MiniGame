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
        bool facePhoto = true;   // a new game takes a screenshot for the face frames
        int  diceDifficulty = 2;  // Liar's Dice: 1 beginner .. 4 master
        int  stbDifficulty = 2;   // Shut the Box: 1 beginner .. 4 master
        int  bjDifficulty = 2;    // Blackjack: 1 beginner .. 4 master
        int  bjRounds = 5;        // Blackjack: rounds in a game
        int  pokerDifficulty = 2;  // Texas Hold'em: 1 beginner .. 4 master
        int  pokerHands = 10;      // Texas Hold'em: hands in a game at most
        int  checkersDifficulty = 2;  // Checkers: 1 beginner .. 4 master, each time the board opens
        int  cribbageDifficulty = 2;  // Cribbage: 1 beginner .. 4 master, each time the board opens
        int  yachtDifficulty = 2;     // Yacht: 1 beginner .. 4 master
        int  gomokuDifficulty = 2;    // Gomoku: 1 beginner .. 4 master
        bool commentary = true;
        int  cooldownSeconds = 8;
        int  voiceWaitSeconds = 15;  // a paced game moves on when the speaker stays silent this long
        bool playerVoice = true;     // Liar's Dice: the player's bids go through SkyrimNet's Transform Dialogue
        bool challenge = true;
        bool chatBox = true;         // a text box under each game's log: the player talks to an opponent
        int  chatMode = 0;           // what is typed there: 0 through Transform Dialogue, 1 said as written
        int  lineLength = 1;         // the opponents' lines about the game: 0 a few words, 1 short, 2 as SkyrimNet has it, 3 longer
    };

    // Re-reads both files. Call from any thread.
    void Reload();

    Values Get();
}
