#pragma once

#include <functional>
#include <string>

// The web UI host that draws the board. Two frameworks render HTML in Skyrim, Magelight UI and
// Prisma UI; the plugin works with whichever is installed, preferring Magelight UI when both are.
// The page is the same for both: it calls the plugin as window.<name>(text), and the plugin runs
// JavaScript in it.
namespace UiHost
{
    enum class Kind
    {
        kNone,
        kPrisma,
        kMagelight,
    };

    // kDataLoaded. Picks the host and creates the (hidden) view. a_onClosed: the host ended the
    // board's turn at the input for good (Magelight: its toggle key, a load) - game thread.
    bool SetUp(void (*a_onClosed)());

    Kind        Which();
    const char* Name();  // "Magelight UI 1.7.1" / "Prisma UI" / "none"
    bool        Ready();

    // Where the page lives, relative to the game folder: the face photos go next to it.
    const char* ViewFolder();

    void Show();
    void Hide();
    bool Focus(bool a_pause);  // the board takes the input (and pauses the game if asked)
    void Unfocus();
    bool HasFocus();  // the board has the input
    bool AnyFocus();  // some view of either host has the input

    void Invoke(const std::string& a_script);
    void Listen(const char* a_name, void (*a_callback)(const char*));

    // Runs a_task on the game thread. For use from a listener: the host calls those inside its
    // frame, where SKSE's own task queue is not always safe to post to.
    void Post(std::function<void()> a_task);
}
