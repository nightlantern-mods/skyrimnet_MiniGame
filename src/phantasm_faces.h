#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The opponents' faces drawn by Phantasm, when it is installed: an SKSE plugin (shipped with
// SeverActions, also on its own) that renders an actor by themselves, head and shoulders on a
// transparent ground. No screenshot, so it works where saving the frame crashes the game (ENB,
// Skyrim Upscaler), and the opponents need not be on screen.
namespace PhantasmFaces
{
    bool        SetUp();  // kDataLoaded: true when Phantasm is there and new enough
    bool        Ready();
    const char* Version();  // "1.0.0", or "" without Phantasm

    // Draws each actor's portrait into a_folder/face_<a_tag>_<FormID>.png and calls a_done(formID,
    // file name) for each, with "" when it could not be drawn. Any thread; a_done runs on a worker
    // thread, one call per actor, in no particular order.
    void Draw(const std::vector<std::uint32_t>& a_ids, std::string a_folder, std::string a_tag,
        std::function<void(std::uint32_t, std::string)> a_done);
}
