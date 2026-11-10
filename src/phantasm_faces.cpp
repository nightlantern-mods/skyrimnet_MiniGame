#include "phantasm_faces.h"

#include "Phantasm_API.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

namespace
{
    // The frames are 180 CSS px (150 at a card table); drawn at twice that, the page scales down.
    constexpr std::uint32_t kSize = 360;
    constexpr std::uint32_t kMinVersion = 200;  // 0.2.0: the first Phantasm with Portrait stills

    const PHANTASM_API::Interface1* g_api = nullptr;
    PHANTASM_API::ClientId          g_client = 0;
    std::string                     g_version;

    // Only stills are asked for, which answer through their own callback: no view events to hear.
    void OnEvent(const PHANTASM_API::Event*, void*) {}

    // ---- PNG with stored (uncompressed) deflate blocks: a few small pictures, no library ----

    std::uint32_t Crc32(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_crc)
    {
        static const auto table = []() {
            std::array<std::uint32_t, 256> t{};
            for (std::uint32_t n = 0; n < 256; ++n) {
                std::uint32_t c = n;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                t[n] = c;
            }
            return t;
        }();
        std::uint32_t c = a_crc ^ 0xFFFFFFFFu;
        for (std::size_t i = 0; i < a_size; ++i) {
            c = table[(c ^ a_data[i]) & 0xFF] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    }

    void Put32(std::vector<std::uint8_t>& a_out, std::uint32_t a_value)
    {
        a_out.push_back(static_cast<std::uint8_t>(a_value >> 24));
        a_out.push_back(static_cast<std::uint8_t>(a_value >> 16));
        a_out.push_back(static_cast<std::uint8_t>(a_value >> 8));
        a_out.push_back(static_cast<std::uint8_t>(a_value));
    }

    void Chunk(std::vector<std::uint8_t>& a_out, const char* a_type, const std::vector<std::uint8_t>& a_data)
    {
        Put32(a_out, static_cast<std::uint32_t>(a_data.size()));
        const auto start = a_out.size();
        a_out.insert(a_out.end(), a_type, a_type + 4);
        a_out.insert(a_out.end(), a_data.begin(), a_data.end());
        Put32(a_out, Crc32(a_out.data() + start, a_out.size() - start, 0));
    }

    // a_rgba: straight (not premultiplied) alpha, a_width * a_height * 4 bytes.
    std::vector<std::uint8_t> EncodePng(std::uint32_t a_width, std::uint32_t a_height, const std::vector<std::uint8_t>& a_rgba)
    {
        // scanlines, each with its filter byte (0: none)
        std::vector<std::uint8_t> raw;
        raw.reserve((static_cast<std::size_t>(a_width) * 4 + 1) * a_height);
        for (std::uint32_t y = 0; y < a_height; ++y) {
            raw.push_back(0);
            const auto row = a_rgba.begin() + static_cast<std::ptrdiff_t>(y) * a_width * 4;
            raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(a_width) * 4);
        }

        // zlib stream of stored blocks (65535 bytes each at most)
        std::vector<std::uint8_t> zlib = { 0x78, 0x01 };
        std::uint32_t             a = 1, b = 0;  // Adler-32
        for (const auto byte : raw) {
            a = (a + byte) % 65521;
            b = (b + a) % 65521;
        }
        for (std::size_t pos = 0; pos < raw.size() || pos == 0;) {
            const auto len = static_cast<std::uint16_t>(std::min<std::size_t>(65535, raw.size() - pos));
            const bool last = pos + len >= raw.size();
            zlib.push_back(last ? 1 : 0);
            zlib.push_back(static_cast<std::uint8_t>(len));
            zlib.push_back(static_cast<std::uint8_t>(len >> 8));
            zlib.push_back(static_cast<std::uint8_t>(~len));
            zlib.push_back(static_cast<std::uint8_t>(~len >> 8));
            zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
            pos += len;
            if (last) {
                break;
            }
        }
        Put32(zlib, (b << 16) | a);

        std::vector<std::uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        std::vector<std::uint8_t> header;
        Put32(header, a_width);
        Put32(header, a_height);
        header.insert(header.end(), { 8, 6, 0, 0, 0 });  // 8 bits, RGBA, deflate, no filter set, no interlace
        Chunk(png, "IHDR", header);
        Chunk(png, "IDAT", zlib);
        Chunk(png, "IEND", {});
        return png;
    }

    float ToLinear(std::uint8_t a_srgb)
    {
        const float c = a_srgb / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    std::uint8_t ToSrgb(float a_linear)
    {
        const float c = a_linear <= 0.0031308f ? a_linear * 12.92f : 1.055f * std::pow(a_linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    // Phantasm's stills are premultiplied by alpha in linear light; PNG wants straight alpha.
    std::vector<std::uint8_t> Unpremultiply(const std::uint8_t* a_rgba, std::uint32_t a_width, std::uint32_t a_height)
    {
        std::vector<std::uint8_t> out(a_rgba, a_rgba + static_cast<std::size_t>(a_width) * a_height * 4);
        for (std::size_t i = 0; i < out.size(); i += 4) {
            const auto alpha = out[i + 3];
            if (alpha == 0) {
                out[i] = out[i + 1] = out[i + 2] = 0;
            } else if (alpha < 255) {
                const float scale = 255.0f / alpha;
                for (int k = 0; k < 3; ++k) {
                    out[i + k] = ToSrgb(ToLinear(out[i + k]) * scale);
                }
            }
        }
        return out;
    }

    struct Job
    {
        std::uint32_t                                  id;
        std::string                                    folder;
        std::string                                    tag;
        std::function<void(std::uint32_t, std::string)> done;
    };

    // Game thread, once per still; the pixels live for the call only.
    void OnStill(const PHANTASM_API::Still* a_still, void* a_user)
    {
        std::unique_ptr<Job> job(static_cast<Job*>(a_user));
        if (!a_still || !a_still->ok || !a_still->rgba) {
            logger::warn("Phantasm did not draw {:08X}: {}", job->id, a_still && a_still->reason ? a_still->reason : "?");
            job->done(job->id, "");
            return;
        }
        const auto width = a_still->width;
        const auto height = a_still->height;
        auto       pixels = Unpremultiply(a_still->rgba, width, height);
        // the file is written off the game thread
        std::thread([job = std::move(job), pixels = std::move(pixels), width, height]() {
            const auto name = std::format("face_{}_{:08X}.png", job->tag, job->id);
            const auto png = EncodePng(width, height, pixels);
            std::ofstream out(std::filesystem::path(job->folder) / name, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
            if (!out) {
                logger::warn("Could not write {}", name);
                job->done(job->id, "");
                return;
            }
            logger::info("Phantasm drew {:08X}: {} ({}x{})", job->id, name, width, height);
            job->done(job->id, name);
        }).detach();
    }
}

namespace PhantasmFaces
{
    bool SetUp()
    {
        using namespace PHANTASM_API;
        const auto module = GetModuleHandleW(L"Phantasm.dll");
        if (!module) {
            logger::info("Phantasm: not installed; faces by screenshot");
            return false;
        }
        const auto get = reinterpret_cast<RequestPluginAPIFn>(GetProcAddress(module, "RequestPluginAPI"));
        const auto api = get ? static_cast<const Interface1*>(get(kInterface1)) : nullptr;
        if (!api) {
            logger::warn("Phantasm is installed but has no plugin API (before 0.2.0); faces by screenshot. Update SeverActions or Phantasm");
            return false;
        }
        const auto v = api->pluginVersion;
        g_version = std::format("{}.{}.{}", v / 10000, v / 100 % 100, v % 100);
        if (v < kMinVersion || api->size < offsetof(Interface1, RequestStillHinted) + sizeof(void*)) {
            logger::warn("Phantasm {} is too old for portraits (0.2.0 or later); faces by screenshot", g_version);
            g_version.clear();
            return false;
        }
        if (const auto result = api->RegisterClient("skyrimnet_minigame", OnEvent, nullptr, &g_client); result != Result::Ok) {
            logger::warn("Phantasm {} did not register the mod ({}); faces by screenshot", g_version, static_cast<int>(result));
            g_version.clear();
            return false;
        }
        g_api = api;
        return true;
    }

    bool Ready()
    {
        return g_api != nullptr;
    }

    const char* Version()
    {
        return g_version.c_str();
    }

    void Draw(const std::vector<std::uint32_t>& a_ids, std::string a_folder, std::string a_tag,
        std::function<void(std::uint32_t, std::string)> a_done)
    {
        if (!g_api) {
            return;
        }
        for (const auto id : a_ids) {
            auto          job = std::make_unique<Job>(Job{ id, a_folder, a_tag, a_done });
            std::uint32_t request = 0;
            // the default look: a slight turn, a little from above, the hearth light, no weapons
            const auto result = g_api->RequestStillHinted(g_client, id, kSize, kSize, PHANTASM_API::Framing::Portrait, nullptr, OnStill, job.get(), &request);
            if (result != PHANTASM_API::Result::Ok) {
                logger::warn("Phantasm did not take the portrait of {:08X} ({})", id, static_cast<int>(result));
                a_done(id, "");
                continue;
            }
            job.release();  // OnStill owns it now
        }
    }
}
