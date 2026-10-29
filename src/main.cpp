#include "PrismaUI_API.h"
#include "settings.h"
#include "skyrimnet_bridge.h"

#include <charconv>
#include <format>
#include <future>
#include <mutex>
#include <ranges>
#include <thread>

#include <nlohmann/json.hpp>

// SkyrimNet MiniGame: mini games against nearby NPCs on a Prisma UI view (Reversi, Liar's Dice).
// The hotkey (MCM, skyrimnet_MiniGamePlayerAlias) calls skyrimnet_MiniGameNative.ToggleBoard(),
// which opens the menu: pick a game, then the opponents from the NPCs nearby. The view runs the
// games and their AI; this plugin lists the NPCs, opens and closes the view, passes the view's
// remarks and game state on to SkyrimNet, and tells the view when one of its players has spoken
// (Liar's Dice waits for each line before it goes on).

PRISMA_UI_API::IVPrismaUI1* PrismaUI;
static PrismaView view;

namespace
{
    constexpr float kChallengeRange = 1000.0f;  // how close an NPC must be to challenge the player
    constexpr float kUnitsPerMetre = 70.0f;

    std::atomic<bool>       g_open = false;
    std::atomic<RE::FormID> g_opponent = 0;  // the first NPC at the table: who speaks when the view names no one

    std::mutex              g_playersLock;
    std::vector<RE::FormID> g_players;  // every NPC at the table

    // "0001A694" -> 0x0001A694, or 0.
    RE::FormID ParseFormID(std::string_view a_hex)
    {
        RE::FormID formID = 0;
        std::from_chars(a_hex.data(), a_hex.data() + a_hex.size(), formID, 16);
        return formID;
    }

    bool AtTable(RE::FormID a_actor)
    {
        std::scoped_lock lock(g_playersLock);
        return std::ranges::find(g_players, a_actor) != g_players.end();
    }

    bool IsHumanoid(RE::Actor* a_actor)
    {
        const auto race = a_actor->GetRace();
        return race && race->HasKeywordString("ActorTypeNPC");
    }

    bool CanPlay(RE::Actor* a_actor)
    {
        return a_actor && !a_actor->IsPlayerRef() && !a_actor->IsDead() && !a_actor->IsDisabled() && a_actor->Is3DLoaded() &&
               !a_actor->IsInCombat() && IsHumanoid(a_actor);
    }

    // A name from the game, as UTF-8. Plugins and string files of some languages are in a
    // Windows code page (Russian 1251, Polish and Czech 1250, western 1252); UTF-8 stays as is.
    std::string GameText(const char* a_text)
    {
        constexpr std::uint32_t kUTF8 = 65001;
        constexpr std::uint32_t kStrict = 0x8;  // MB_ERR_INVALID_CHARS
        const std::string_view  text = a_text ? a_text : "";
        if (text.empty() || REX::W32::MultiByteToWideChar(kUTF8, kStrict, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0) {
            return std::string(text);
        }
        static const std::uint32_t codePage = []() -> std::uint32_t {
            const auto ini = RE::INISettingCollection::GetSingleton();
            const auto setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
            std::string language = setting && setting->GetString() ? setting->GetString() : "";
            std::ranges::transform(language, language.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return language == "RUSSIAN"                       ? 1251 :
                   language == "POLISH" || language == "CZECH" ? 1250 :
                                                                 1252;
        }();
        const int    size = static_cast<int>(text.size());
        std::wstring wide(REX::W32::MultiByteToWideChar(codePage, 0, text.data(), size, nullptr, 0), L'\0');
        REX::W32::MultiByteToWideChar(codePage, 0, text.data(), size, wide.data(), static_cast<int>(wide.size()));
        std::string out(REX::W32::WideCharToMultiByte(kUTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr), '\0');
        REX::W32::WideCharToMultiByte(kUTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
        return out;
    }

    // Game thread: the NPCs who could sit down to play, nearest first, within a_radius.
    // [{"id": "0001A694", "name": ..., "distance": metres, "crosshair": bool}]
    nlohmann::json GatherActors(float a_radius)
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        RE::Actor* crosshair = nullptr;
        if (const auto pick = RE::CrosshairPickData::GetSingleton()) {
            if (const auto ref = pick->GetActiveTarget().get()) {
                crosshair = ref->As<RE::Actor>();
            }
        }

        std::vector<std::pair<float, RE::Actor*>> nearby;
        if (const auto processLists = RE::ProcessLists::GetSingleton()) {
            processLists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (CanPlay(a_actor)) {
                    const float d = player->GetPosition().GetDistance(a_actor->GetPosition());
                    if (d <= a_radius) {
                        nearby.emplace_back(d, a_actor);
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        std::ranges::sort(nearby, {}, &std::pair<float, RE::Actor*>::first);

        auto actors = nlohmann::json::array();
        for (const auto& [distance, actor] : nearby) {
            actors.push_back({
                { "id", std::format("{:08X}", actor->GetFormID()) },
                { "name", GameText(actor->GetDisplayFullName()) },
                { "distance", static_cast<int>(distance / kUnitsPerMetre + 0.5f) },
                { "crosshair", actor == crosshair },
            });
        }
        return actors;
    }

    void Invoke(const std::string& a_script)
    {
        PrismaUI->Invoke(view, a_script.c_str());
    }

    void CloseBoard()
    {
        if (!PrismaUI || !g_open.exchange(false)) {
            return;
        }
        Invoke("rvHidden()");
        PrismaUI->Unfocus(view);
        PrismaUI->Hide(view);
    }

    // The view's texts: the "view" object of SKSE/Plugins/skyrimnet_MiniGame/strings.json (a
    // translation mod overrides the file); the view has English built in for anything missing.
    // Read on every open, so an edited file needs no restart.
    nlohmann::json LoadViewStrings()
    {
        std::ifstream in("Data/SKSE/Plugins/skyrimnet_MiniGame/strings.json", std::ios::binary);
        const auto json = nlohmann::json::parse(in, nullptr, false);
        return json.is_object() ? json.value("view", nlohmann::json::object()) : nlohmann::json::object();
    }

    // What every open tells the view: the player's name, the MCM settings and the texts.
    // voice: narrations reach SkyrimNet, so paced games wait for the lines (up to voiceWait s).
    nlohmann::json ViewOptions(const Settings::Values& a_settings)
    {
        static constexpr const char* kColors[] = { "black", "white", "random", "alternate" };
        return {
            { "player", GameText(RE::PlayerCharacter::GetSingleton()->GetDisplayFullName()) },
            { "level", a_settings.difficulty },
            { "hints", a_settings.hints },
            { "color", kColors[a_settings.color] },
            { "diceLevel", a_settings.diceDifficulty },
            { "voice", SkyrimNetBridge::CanNarrate() },
            { "playerVoice", SkyrimNetBridge::CanPlayerSay() },
            { "voiceWait", a_settings.voiceWaitSeconds },
            { "gather", a_settings.gather },
            { "strings", LoadViewStrings() },
        };
    }

    // Shows the view, runs a_function(a_options) in it and gives it the focus.
    void ShowView(const char* a_function, const nlohmann::json& a_options, bool a_pause)
    {
        const auto script = std::string(a_function) + "(" + a_options.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + ")";
        SKSE::GetTaskInterface()->AddUITask([script, a_pause]() {
            if (g_open.exchange(true)) {
                return;
            }
            PrismaUI->Show(view);
            Invoke(script);
            if (!PrismaUI->Focus(view, a_pause)) {
                logger::warn("Focus on the view failed");
            }
        });
    }

    // Game thread: the menu, with the NPCs who could play.
    void OpenMenu()
    {
        Settings::Reload();
        const auto settings = Settings::Get();
        auto       options = ViewOptions(settings);
        options["actors"] = GatherActors(static_cast<float>(settings.scanRadius));
        ShowView("mgOpen", options, settings.pauseGame);
    }

    // Game thread: a_opponent challenges the player to Reversi; the player may still decline.
    void OpenChallenge(RE::Actor* a_opponent)
    {
        Settings::Reload();
        const auto settings = Settings::Get();
        auto       options = ViewOptions(settings);
        options["opponent"] = { { "id", std::format("{:08X}", a_opponent->GetFormID()) }, { "name", GameText(a_opponent->GetDisplayFullName()) } };
        ShowView("rvOpen", options, settings.pauseGame);
    }

    // ---- opponents' faces ----
    //
    // A new game takes a photo of its opponents for the face frames: the view fades to black and
    // sends mgGather; the opponents are called to stand in front of the player, facing them (MCM
    // bGather); the view clears the screen (mgUncover); the HUD is hidden for a moment and the
    // frame saved as a JPG next to the view; the view crops each face out of it (mgFaces).

    constexpr const char* kViewFolder = "Data/PrismaUI/views/skyrimnet_MiniGame";
    constexpr const char* kHeadNode = "NPC Head [Head]";

    std::atomic<int> g_photoCount = 0;

    // Where a_actor's face is on screen: {x, y, w, h} as fractions of the screen, top left
    // origin, or null when it is off screen, behind something or too small to make out.
    nlohmann::json FaceCrop(RE::Actor* a_actor, bool a_log = false)
    {
        const auto root = a_actor->Get3D();
        const auto head = root ? root->GetObjectByName(kHeadNode) : nullptr;
        const auto camera = RE::Main::WorldRootCamera();
        const auto player = RE::PlayerCharacter::GetSingleton();
        bool       unused = false;
        if (!head || !camera) {
            if (a_log) {
                logger::info("  {:08X}: no head node", a_actor->GetFormID());
            }
            return nullptr;
        }
        if (!player->HasLineOfSight(a_actor, unused)) {
            if (a_log) {
                logger::info("  {:08X}: not in line of sight", a_actor->GetFormID());
            }
            return nullptr;
        }
        const RE::NiRect<float> port{ 0.0f, 1.0f, 1.0f, 0.0f };
        const auto              centre = head->world.translate + RE::NiPoint3(0.0f, 0.0f, 4.0f);
        const auto              crown = centre + RE::NiPoint3(0.0f, 0.0f, 12.0f);
        float                   x, y, z, x2, y2, z2;
        if (!RE::NiCamera::WorldPtToScreenPt3(camera->GetRuntimeData().worldToCam, port, centre, x, y, z, 1e-5f) ||
            !RE::NiCamera::WorldPtToScreenPt3(camera->GetRuntimeData().worldToCam, port, crown, x2, y2, z2, 1e-5f) || z < 0.0f) {
            if (a_log) {
                logger::info("  {:08X}: behind the camera", a_actor->GetFormID());
            }
            return nullptr;
        }
        const auto  screen = RE::BSGraphics::Renderer::GetScreenSize();
        const float aspect = screen.height ? static_cast<float>(screen.width) / static_cast<float>(screen.height) : 16.0f / 9.0f;
        const float h = std::abs(y2 - y) * 3.0f;  // of the screen height
        const float w = h / aspect;
        const float left = x - w / 2.0f;
        const float top = (1.0f - y) - h / 2.0f;
        if (h * static_cast<float>(screen.height) < 24.0f || left < 0.0f || top < 0.0f || left + w > 1.0f || top + h > 1.0f) {
            if (a_log) {
                logger::info("  {:08X}: off screen or too small (x {:.2f} y {:.2f} h {:.3f})", a_actor->GetFormID(), x, 1.0f - y, h);
            }
            return nullptr;
        }
        return { { "x", left }, { "y", top }, { "w", w }, { "h", h } };
    }

    std::vector<RE::Actor*> LookUpActors(const std::vector<RE::FormID>& a_ids)
    {
        std::vector<RE::Actor*> actors;
        for (const auto id : a_ids) {
            if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
                actors.push_back(actor);
            }
        }
        return actors;
    }

    // Game thread: a_actor turns to face the player.
    void FacePlayer(RE::Actor* a_actor)
    {
        const auto player = RE::PlayerCharacter::GetSingleton()->GetPosition();
        const auto at = a_actor->GetPosition();
        a_actor->SetHeading(std::atan2(player.x - at.x, player.y - at.y));
    }

    // Game thread: opponents sitting, leaning or working at something get up first, so that
    // they can be moved. True when anyone did.
    bool StandUpOpponents(const std::vector<RE::Actor*>& a_actors)
    {
        bool any = false;
        for (const auto actor : a_actors | std::views::take(3)) {
            if (CanPlay(actor) && actor->GetOccupiedFurniture()) {
                logger::info("  {:08X}: gets up from furniture", actor->GetFormID());
                actor->StopInteractingQuick(true);
                any = true;
            }
        }
        return any;
    }

    // Game thread: a_actors stand in an arc in front of the player, on the side the camera
    // looks at, facing the player. Anyone already close and in view stays put.
    void GatherOpponents(const std::vector<RE::Actor*>& a_actors)
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto camera = RE::Main::WorldRootCamera();
        // a heading: 0 is north (+Y), clockwise; the camera looks along its own X axis
        float yaw = player->GetAngleZ();
        if (camera) {
            const auto& r = camera->world.rotate;
            if (std::abs(r.entry[0][0]) + std::abs(r.entry[1][0]) > 0.01f) {
                yaw = std::atan2(r.entry[0][0], r.entry[1][0]);
            }
        }
        // narrow enough that all of them are on screen; at 0.6 the outer two were cut off
        static constexpr float kSpread[4][3] = { {}, { 0.0f }, { -0.22f, 0.22f }, { -0.38f, 0.0f, 0.38f } };
        const auto             count = std::min<std::size_t>(a_actors.size(), 3);
        const auto             origin = player->GetPosition();
        for (std::size_t i = 0; i < count; ++i) {
            const auto actor = a_actors[i];
            if (!CanPlay(actor)) {
                logger::info("  {:08X}: cannot be moved", actor->GetFormID());
                continue;
            }
            const float distance = origin.GetDistance(actor->GetPosition());
            if (distance < 300.0f && !actor->GetOccupiedFurniture() && !FaceCrop(actor).is_null()) {
                logger::info("  {:08X}: already in view, {:.0f} away", actor->GetFormID(), distance);
                continue;
            }
            logger::info("  {:08X}: moved, was {:.0f} away", actor->GetFormID(), distance);
            const float  angle = yaw + kSpread[count][i];
            RE::NiPoint3 spot = origin + RE::NiPoint3(std::sin(angle), std::cos(angle), 0.0f) * 190.0f;
            spot.z += 10.0f;
            actor->SetPosition(spot, true);
            FacePlayer(actor);
        }
    }

    // Game thread: saves the frame and tells the view where each face is on it.
    void TakePhoto(const std::vector<RE::FormID>& a_ids)
    {
        const auto      n = ++g_photoCount;
        const auto      name = std::format("face_{}.jpg", n);
        std::error_code ec;
        std::filesystem::remove(std::format("{}/face_{}.jpg", kViewFolder, n - 1), ec);
        RE::BSGraphics::Renderer::GetSingleton()->SaveRenderTargetToFile(RE::RENDER_TARGETS::kFRAMEBUFFER,
            std::format("{}/{}", kViewFolder, name).c_str(), RE::BSGraphics::TextureFileFormat::kJPG);
        if (const auto ui = RE::UI::GetSingleton()) {
            ui->ShowMenus(true);
        }

        auto faces = nlohmann::json::object();
        for (const auto actor : LookUpActors(a_ids)) {
            if (auto crop = FaceCrop(actor, true); !crop.is_null()) {
                faces[std::format("{:08X}", actor->GetFormID())] = std::move(crop);
            }
        }
        logger::info("photo {}: {} of {} faces", name, faces.size(), a_ids.size());
        const nlohmann::json photo = { { "file", name }, { "faces", faces } };
        SKSE::GetTaskInterface()->AddUITask([script = "mgFaces(" + photo.dump() + ")"]() { Invoke(script); });
    }

    // From the view: a new game's opponents, once the screen is black.
    void OnGather(std::vector<RE::FormID> a_ids)
    {
        std::thread([ids = std::move(a_ids)]() {
            using namespace std::chrono_literals;
            if (Settings::Get().gather) {
                logger::info("gathering {} opponents", ids.size());
                auto standing = std::make_shared<std::promise<bool>>();
                auto stood = standing->get_future();
                SKSE::GetTaskInterface()->AddTask([ids, standing]() { standing->set_value(StandUpOpponents(LookUpActors(ids))); });
                if (stood.wait_for(2s) == std::future_status::ready && stood.get()) {
                    std::this_thread::sleep_for(400ms);  // out of the seat before moving
                }
                SKSE::GetTaskInterface()->AddTask([ids]() { GatherOpponents(LookUpActors(ids)); });
                std::this_thread::sleep_for(700ms);  // they land and turn
            }
            SKSE::GetTaskInterface()->AddUITask([]() { Invoke("mgUncover()"); });
            SKSE::GetTaskInterface()->AddTask([ids, gather = Settings::Get().gather]() {
                if (gather) {
                    // again: someone just out of a seat may have turned back while getting up
                    for (const auto actor : LookUpActors(ids) | std::views::take(3)) {
                        if (CanPlay(actor) && !actor->GetOccupiedFurniture()) {
                            FacePlayer(actor);
                        }
                    }
                }
                if (const auto ui = RE::UI::GetSingleton()) {
                    ui->ShowMenus(false);  // no HUD or crosshair on the photo
                }
            });
            std::this_thread::sleep_for(450ms);  // the view's fade, and a few frames without the HUD
            SKSE::GetTaskInterface()->AddTask([ids]() { TakePhoto(ids); });
        }).detach();
    }

    // Papyrus: skyrimnet_MiniGameNative.ToggleBoard(), from the MCM hotkey.
    void ToggleBoardNative(RE::StaticFunctionTag*)
    {
        if (!PrismaUI) {
            logger::warn("Hotkey pressed, but Prisma UI is not available");
            return;
        }
        if (g_open) {
            SKSE::GetTaskInterface()->AddUITask(CloseBoard);
            return;
        }
        // not over the inventory, map, dialogue and the like
        if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return;
        }
        SKSE::GetTaskInterface()->AddTask(OpenMenu);
    }

    // Papyrus: skyrimnet_MiniGameNative.ReloadSettings(), whenever an MCM setting changes.
    void ReloadSettingsNative(RE::StaticFunctionTag*)
    {
        Settings::Reload();
    }

    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
    {
        a_vm->RegisterFunction("ToggleBoard", "skyrimnet_MiniGameNative", ToggleBoardNative);
        a_vm->RegisterFunction("ReloadSettings", "skyrimnet_MiniGameNative", ReloadSettingsNative);
        return true;
    }

    // SkyrimNet: may this NPC challenge the player now? Runs on SkyrimNet's threads.
    bool CanChallenge(RE::Actor* a_actor)
    {
        if (!Settings::Get().challenge || g_open || !PrismaUI) {
            return false;
        }
        const auto player = RE::PlayerCharacter::GetSingleton();
        return CanPlay(a_actor) && player && player->GetPosition().GetDistance(a_actor->GetPosition()) <= kChallengeRange;
    }

    // SkyrimNet: the NPC challenges the player. The board opens with the challenge once no menu
    // is up (SkyrimNet may decide this while one is), or not at all after a while.
    void OnChallenge(RE::FormID a_actor)
    {
        std::thread([a_actor]() {
            for (int tries = 0; tries < 40; ++tries) {
                const auto ui = RE::UI::GetSingleton();
                if (ui && !ui->GameIsPaused()) {
                    SKSE::GetTaskInterface()->AddTask([a_actor]() {
                        const auto actor = RE::TESForm::LookupByID<RE::Actor>(a_actor);
                        if (!g_open && CanPlay(actor)) {
                            OpenChallenge(actor);
                        }
                    });
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            logger::info("Challenge from {:08X} dropped: a menu stayed open", a_actor);
        }).detach();
    }

    // A loaded save has its own story: close the board and forget the game on it.
    class LoadWatcher : public RE::BSTEventSink<RE::TESLoadGameEvent>
    {
    public:
        static LoadWatcher* GetSingleton()
        {
            static LoadWatcher singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
        {
            SKSE::GetTaskInterface()->AddUITask([]() {
                CloseBoard();
                Invoke("rvReset()");
                g_opponent = 0;
                std::scoped_lock lock(g_playersLock);
                g_players.clear();
            });
            SkyrimNetBridge::Reset();
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // Esc on the board does what the view's Esc does: the view does not always get the key
    // itself while it has the focus.
    class EscapeWatcher : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static EscapeWatcher* GetSingleton()
        {
            static EscapeWatcher singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
        {
            if (!g_open || !a_events) {
                return RE::BSEventNotifyControl::kContinue;
            }
            for (auto event = *a_events; event; event = event->next) {
                const auto button = event->AsButtonEvent();
                if (button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
                    button->GetIDCode() == RE::BSKeyboardDevice::Key::kEscape && button->IsDown()) {
                    SKSE::GetTaskInterface()->AddUITask([]() {
                        if (g_open && PrismaUI->HasFocus(view)) {
                            Invoke("mgEscape()");
                        }
                    });
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

static void SetUp()
{
    Settings::Reload();

    // last session's photos
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kViewFolder, ec)) {
        if (entry.path().filename().string().starts_with("face_")) {
            std::filesystem::remove(entry.path(), ec);
        }
    }

    PrismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
    if (!PrismaUI) {
        logger::critical("PrismaUI API not available. Is Prisma UI installed and enabled?");
        return;
    }

    view = PrismaUI->CreateView("skyrimnet_MiniGame/index.html", [](PrismaView a_view) -> void {
        logger::info("View DOM is ready {}", a_view);
    });
    PrismaUI->Hide(view);

    PrismaUI->RegisterJSListener(view, "rvClose", [](const char*) -> void {
        SKSE::GetTaskInterface()->AddUITask(CloseBoard);
    });
    // The opponents of the game about to be played, as hex FormIDs "0001A694,..." (Reversi: one).
    // Sent before the game's first remark, so remarks go to the right NPC.
    PrismaUI->RegisterJSListener(view, "mgPlayers", [](const char* data) -> void {
        std::vector<RE::FormID> players;
        for (const auto part : std::views::split(std::string_view(data ? data : ""), ',')) {
            if (const auto formID = ParseFormID(std::string_view(part.begin(), part.end()))) {
                players.push_back(formID);
            }
        }
        g_opponent = players.empty() ? 0 : players.front();
        std::scoped_lock lock(g_playersLock);
        g_players = std::move(players);
    });
    // A new game's opponents "0001A694,...", with the screen faded to black: call them over and
    // take their photo.
    PrismaUI->RegisterJSListener(view, "mgGather", [](const char* data) -> void {
        std::vector<RE::FormID> ids;
        for (const auto part : std::views::split(std::string_view(data ? data : ""), ',')) {
            if (const auto formID = ParseFormID(std::string_view(part.begin(), part.end()))) {
                ids.push_back(formID);
            }
        }
        OnGather(std::move(ids));
    });
    // {"text": "...", "level": "important" | "notable" | "minor", "speaker": "0001A694" (optional:
    // the first opponent)}
    PrismaUI->RegisterJSListener(view, "rvNarrate", [](const char* data) -> void {
        const auto json = nlohmann::json::parse(data ? data : "", nullptr, false);
        if (!json.is_object() || !json.contains("text") || !json["text"].is_string()) {
            return;
        }
        const auto level = json.value("level", std::string("minor"));
        const auto speaker = json.contains("speaker") && json["speaker"].is_string() ? ParseFormID(json["speaker"].get<std::string>()) : RE::FormID(0);
        SkyrimNetBridge::Narrate(speaker ? speaker : g_opponent.load(), json["text"].get<std::string>(),
            level == "important" ? SkyrimNetBridge::Level::kImportant :
            level == "notable"   ? SkyrimNetBridge::Level::kNotable :
                                   SkyrimNetBridge::Level::kMinor);
    });
    // What the player means to say, said in their own words.
    PrismaUI->RegisterJSListener(view, "mgPlayerSay", [](const char* data) -> void {
        SkyrimNetBridge::PlayerSay(data ? data : "");
    });
    // {"states": {"0001A694": "what this NPC knows of the game", ...}, "playing": bool}
    PrismaUI->RegisterJSListener(view, "rvState", [](const char* data) -> void {
        const auto json = nlohmann::json::parse(data ? data : "", nullptr, false);
        if (!json.is_object() || !json.contains("states") || !json["states"].is_object()) {
            return;
        }
        std::unordered_map<RE::FormID, std::string> states;
        for (const auto& [id, text] : json["states"].items()) {
            if (text.is_string()) {
                states[ParseFormID(id)] = text.get<std::string>();
            }
        }
        SkyrimNetBridge::SetStates(std::move(states), json.value("playing", false));
    });

    if (const auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
        events->AddEventSink<RE::TESLoadGameEvent>(LoadWatcher::GetSingleton());
    }
    if (const auto input = RE::BSInputDeviceManager::GetSingleton()) {
        input->AddEventSink(EscapeWatcher::GetSingleton());
    }

    // mgSpoke({"id": "0001A694", "text": "..."}): someone at the table said a line.
    SkyrimNetBridge::Init(CanChallenge, OnChallenge, [](RE::FormID a_speaker, std::string a_text) {
        if (!g_open || (a_speaker != 0x14 && !AtTable(a_speaker))) {  // 0x14: the player
            return;
        }
        const nlohmann::json spoke = { { "id", std::format("{:08X}", a_speaker) }, { "text", a_text } };
        const auto script = "mgSpoke(" + spoke.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + ")";
        SKSE::GetTaskInterface()->AddUITask([script]() { Invoke(script); });
    });
    logger::info("Board ready");
}

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        SetUp();
    }
}

extern "C" DLLEXPORT bool SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();

    auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));
    if (!g_messaging) {
        logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
        return false;
    }

    SKSE::Init(a_skse);
    spdlog::default_logger()->flush_on(spdlog::level::trace);
    logger::info("skyrimnet_MiniGame loaded");

    SKSE::GetPapyrusInterface()->Register(RegisterPapyrus);
    g_messaging->RegisterListener("SKSE", SKSEMessageHandler);
    return true;
}
