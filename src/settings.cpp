#include "settings.h"

#include <mutex>
#include <unordered_map>

namespace Settings
{
    namespace
    {
        constexpr const char* kDefaultsFile = "Data/MCM/Config/skyrimnet_MiniGame/settings.ini";
        constexpr const char* kUserFile = "Data/MCM/Settings/skyrimnet_MiniGame.ini";

        std::mutex g_lock;
        Values     g_values;

        std::string Trim(std::string_view a_text)
        {
            const auto first = a_text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = a_text.find_last_not_of(" \t\r\n");
            return std::string(a_text.substr(first, last - first + 1));
        }

        // "Section:key" -> value, for every key in the file. A missing file adds nothing.
        void ReadIni(const char* a_path, std::unordered_map<std::string, std::string>& a_out)
        {
            std::ifstream in(a_path);
            std::string   line;
            std::string   section;
            while (std::getline(in, line)) {
                const auto text = Trim(line);
                if (text.empty() || text[0] == ';' || text[0] == '#') {
                    continue;
                }
                if (text.front() == '[' && text.back() == ']') {
                    section = Trim(std::string_view(text).substr(1, text.size() - 2));
                    continue;
                }
                const auto eq = text.find('=');
                if (eq != std::string::npos) {
                    a_out[section + ":" + Trim(std::string_view(text).substr(0, eq))] = Trim(std::string_view(text).substr(eq + 1));
                }
            }
        }

        int ToInt(const std::unordered_map<std::string, std::string>& a_ini, const char* a_key, int a_default)
        {
            const auto it = a_ini.find(a_key);
            if (it == a_ini.end()) {
                return a_default;
            }
            try {
                return std::stoi(it->second);
            } catch (...) {
                return a_default;
            }
        }
    }

    void Reload()
    {
        std::unordered_map<std::string, std::string> ini;
        ReadIni(kDefaultsFile, ini);
        ReadIni(kUserFile, ini);  // the user's values win

        Values v;
        v.difficulty = std::clamp(ToInt(ini, "General:iDifficulty", v.difficulty), 1, 4);
        v.color = std::clamp(ToInt(ini, "General:iColor", v.color), 0, 3);
        v.hints = ToInt(ini, "General:bHints", v.hints) != 0;
        v.pauseGame = ToInt(ini, "General:bPauseGame", v.pauseGame) != 0;
        v.scanRadius = std::clamp(ToInt(ini, "General:iScanRadius", v.scanRadius), 300, 6000);
        v.gather = ToInt(ini, "General:bGather", v.gather) != 0;
        v.diceDifficulty = std::clamp(ToInt(ini, "LiarsDice:iDifficulty", v.diceDifficulty), 1, 4);
        v.commentary = ToInt(ini, "SkyrimNet:bCommentary", v.commentary) != 0;
        v.cooldownSeconds = std::clamp(ToInt(ini, "SkyrimNet:iCooldown", v.cooldownSeconds), 0, 600);
        v.voiceWaitSeconds = std::clamp(ToInt(ini, "SkyrimNet:iVoiceWait", v.voiceWaitSeconds), 3, 60);
        v.playerVoice = ToInt(ini, "SkyrimNet:bPlayerVoice", v.playerVoice) != 0;
        v.challenge = ToInt(ini, "SkyrimNet:bChallenge", v.challenge) != 0;

        std::scoped_lock lock(g_lock);
        g_values = v;
    }

    Values Get()
    {
        std::scoped_lock lock(g_lock);
        return g_values;
    }
}
