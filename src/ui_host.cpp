#include "ui_host.h"

#include "PrismaUI_API.h"  // first: it sets NOMINMAX and WIN32_LEAN_AND_MEAN before <windows.h>
#include "MagelightUI_API.h"

#include <filesystem>

namespace
{
    constexpr const char* kModId = "skyrimnet_MiniGame";
    constexpr const char* kPrismaFolder = "Data/PrismaUI/views/skyrimnet_MiniGame";
    constexpr const char* kMagelightFolder = "Data/Magelight/skyrimnet_MiniGame/views";

    UiHost::Kind g_kind = UiHost::Kind::kNone;
    std::string  g_name = "none";
    void (*g_onClosed)() = nullptr;

    // Prisma UI. Looked up under Magelight too: SkyrimNet's own views are Prisma UI views, and
    // neither host sees the other's focus.
    PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
    PrismaView                  g_prismaView = 0;

    const MAGELIGHT_API::MagelightApi4* g_mage = nullptr;
    MAGELIGHT_API::ModId                g_mod = 0;
    MAGELIGHT_API::ViewId               g_view = 0;

    // ---- Magelight UI ----

    // Game thread (ModDesc.callbackThread).
    void OnMagelightEvent(const MAGELIGHT_API::EventData* a_event, void*)
    {
        using Event = MAGELIGHT_API::Event;
        const auto detail = a_event->detail ? a_event->detail : "";
        switch (a_event->type) {
        case Event::ViewDomReady:
            logger::info("View DOM is ready {}", a_event->view);
            break;
        case Event::ViewLoadFailed:
            logger::error("The view failed to load: {}", detail);
            break;
        case Event::UIModeExited:
            if (a_event->view == g_view && g_onClosed) {
                g_onClosed();
            }
            break;
        case Event::UIModeRefused:
            logger::warn("Magelight did not give the board the input: {}", detail);
            break;
        case Event::FocusDenied:
            logger::info("Another mod asked for the input while the board had it: {}", detail);
            break;
        case Event::ConsoleMessage:
            if (a_event->x == 2) {
                logger::warn("View console error (line {}): {}", a_event->y, detail);
            }
            break;
        case Event::RenderDead:
            logger::error("Magelight's overlay shut down for this session; the board cannot be drawn");
            break;
        default:
            break;
        }
    }

    void OnMagelightLog(int a_level, const char* a_message, void*)
    {
        const auto message = a_message ? a_message : "";
        switch (a_level) {
        case 2:
            logger::error("Magelight: {}", message);
            break;
        case 1:
            logger::warn("Magelight: {}", message);
            break;
        default:
            logger::info("Magelight: {}", message);
            break;
        }
    }

    // UTF-8, absolute: Magelight resolves relative paths against its own folder.
    std::string AbsolutePath(const char* a_relative)
    {
        std::error_code ec;
        const auto      path = std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(a_relative)), ec);
        const auto      utf8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }

    bool SetUpMagelight()
    {
        using namespace MAGELIGHT_API;
        g_mage = RequestApi4();
        if (!g_mage) {
            return false;
        }
        const auto host = g_mage->hostVersion ? g_mage->hostVersion : "?";

        ModDesc mod{};
        mod.size = sizeof(mod);
        mod.modId = kModId;
        mod.displayName = "SkyrimNet MiniGame";
        mod.minHostVersion = PackVersion(1, 0, 0);
        mod.callbackThread = CallbackThread::GameThread;
        mod.onEvent = OnMagelightEvent;
        mod.onLog = OnMagelightLog;
        if (const auto result = g_mage->RegisterMod(&mod, &g_mod); result != Result::Ok) {
            if (result == Result::HostTooOld) {
                logger::error("Magelight UI {} is too old for this mod (1.0.0 or later); trying Prisma UI", host);
            } else {
                logger::error("Magelight UI {} did not register the mod ({}); trying Prisma UI", host, static_cast<int>(result));
            }
            g_mage = nullptr;
            return false;
        }

        const auto html = AbsolutePath((std::string(kMagelightFolder) + "/index.html").c_str());
        ViewDesc   view{};
        view.size = sizeof(view);
        view.name = "board";
        view.htmlPath = html.c_str();
        view.fullscreen = true;
        view.startVisible = false;
        view.layer = Layer::Panel;
        if (const auto result = g_mage->CreateViewEx(g_mod, &view, &g_view); result != Result::Ok) {
            logger::error("Magelight UI {} did not create the view ({}): {}", host, static_cast<int>(result), g_mage->GetLastErrorMessage(g_mod));
            g_mage->UnregisterMod(g_mod);
            g_mage = nullptr;
            g_mod = 0;
            return false;
        }
        // Esc goes to the board (it steps back through its menus); the host would end the UI mode.
        g_mage->SetEscapeCapture(g_view, true);

        g_kind = UiHost::Kind::kMagelight;
        g_name = std::string("Magelight UI ") + host;
        return true;
    }

    // ---- Prisma UI ----

    bool SetUpPrisma()
    {
        if (!g_prisma) {
            return false;
        }
        g_prismaView = g_prisma->CreateView("skyrimnet_MiniGame/index.html", [](PrismaView a_view) -> void {
            logger::info("View DOM is ready {}", a_view);
        });
        g_prisma->Hide(g_prismaView);
        g_kind = UiHost::Kind::kPrisma;
        g_name = "Prisma UI";
        return true;
    }
}

namespace UiHost
{
    bool SetUp(void (*a_onClosed)())
    {
        g_onClosed = a_onClosed;
        g_prisma = static_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
        if (SetUpMagelight() || SetUpPrisma()) {
            logger::info("UI host: {}{}", g_name, g_kind == Kind::kMagelight && g_prisma ? " (Prisma UI is installed too)" : "");
            return true;
        }
        logger::critical("Neither Magelight UI nor Prisma UI is available. Is one of them installed and enabled?");
        return false;
    }

    Kind Which()
    {
        return g_kind;
    }

    const char* Name()
    {
        return g_name.c_str();
    }

    bool Ready()
    {
        return g_kind != Kind::kNone;
    }

    const char* ViewFolder()
    {
        return g_kind == Kind::kMagelight ? kMagelightFolder : kPrismaFolder;
    }

    void Show()
    {
        switch (g_kind) {
        case Kind::kMagelight:
            g_mage->ShowView(g_view, true);
            break;
        case Kind::kPrisma:
            g_prisma->Show(g_prismaView);
            break;
        default:
            break;
        }
    }

    void Hide()
    {
        switch (g_kind) {
        case Kind::kMagelight:
            g_mage->ShowView(g_view, false);
            break;
        case Kind::kPrisma:
            g_prisma->Hide(g_prismaView);
            break;
        default:
            break;
        }
    }

    bool Focus(bool a_pause)
    {
        switch (g_kind) {
        case Kind::kMagelight: {
            // Ok: the entry is queued; UIModeEntered or UIModeRefused follows as an event.
            const auto result = g_mage->RequestUIMode(g_view, a_pause ? MAGELIGHT_API::kUIModeFlagPause : MAGELIGHT_API::kUIModeFlagNone);
            if (result != MAGELIGHT_API::Result::Ok) {
                logger::warn("Magelight did not take the board's focus request ({}): {}", static_cast<int>(result), g_mage->GetLastErrorMessage(g_mod));
            }
            return result == MAGELIGHT_API::Result::Ok;
        }
        case Kind::kPrisma:
            return g_prisma->Focus(g_prismaView, a_pause);
        default:
            return false;
        }
    }

    void Unfocus()
    {
        switch (g_kind) {
        case Kind::kMagelight:
            if (g_mage->GetUIModeOwner() == g_mod) {
                g_mage->ReleaseUIMode(g_mod);
            }
            break;
        case Kind::kPrisma:
            g_prisma->Unfocus(g_prismaView);
            break;
        default:
            break;
        }
    }

    bool HasFocus()
    {
        switch (g_kind) {
        case Kind::kMagelight:
            return g_mage->GetUIModeOwner() == g_mod;
        case Kind::kPrisma:
            return g_prisma->HasFocus(g_prismaView);
        default:
            return false;
        }
    }

    bool AnyFocus()
    {
        const bool prisma = g_prisma && g_prisma->HasAnyActiveFocus();
        return prisma || (g_mage && g_mage->IsUIModeActive());
    }

    void Invoke(const std::string& a_script)
    {
        switch (g_kind) {
        case Kind::kMagelight:
            g_mage->InvokeJS(g_view, a_script.c_str());
            break;
        case Kind::kPrisma:
            g_prisma->Invoke(g_prismaView, a_script.c_str());
            break;
        default:
            break;
        }
    }

    void Listen(const char* a_name, void (*a_callback)(const char*))
    {
        switch (g_kind) {
        case Kind::kMagelight:
            g_mage->RegisterJSListener(g_view, a_name, a_callback);
            break;
        case Kind::kPrisma:
            g_prisma->RegisterJSListener(g_prismaView, a_name, a_callback);
            break;
        default:
            break;
        }
    }

    void Post(std::function<void()> a_task)
    {
        // Magelight 0.30.1+: a post that never waits on SKSE's task lock from inside the frame.
        if (g_kind == Kind::kMagelight && g_mage->hostVersionNumber >= MAGELIGHT_API::PackVersion(0, 30, 1)) {
            auto       boxed = new std::function<void()>(std::move(a_task));
            const auto result = g_mage->PostGameTask(
                [](void* a_user) {
                    const auto task = static_cast<std::function<void()>*>(a_user);
                    (*task)();
                    delete task;
                },
                boxed);
            if (result != MAGELIGHT_API::Result::Ok) {
                logger::warn("Magelight did not take a game task ({})", static_cast<int>(result));
                delete boxed;
            }
            return;
        }
        SKSE::GetTaskInterface()->AddUITask(std::move(a_task));
    }
}
