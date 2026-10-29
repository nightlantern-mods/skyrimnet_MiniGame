-- set minimum xmake version
set_xmakever("3.0.0")

-- CommonLibSSE-NG: a checkout shared with the other Prisma UI plugins (override with COMMONLIB_SSE_NG)
includes(os.getenv("COMMONLIB_SSE_NG") or "C:/Modding/MotionCommander/lib/commonlibsse-ng")

local PLUGIN_NAME = "skyrimnet_MiniGame"
local PLUGIN_VERSION = "0.1.0"

local VIEW_NAME = "skyrimnet_MiniGame"

set_project(PLUGIN_NAME)
set_version(PLUGIN_VERSION)
set_license("MIT")

set_languages("c++23")
set_warnings("allextra")
add_defines("UNICODE", "_UNICODE")

set_policy("package.requires_lock", true)

add_requires("nlohmann_json v3.12.0")

add_rules("mode.release")
add_rules("plugin.vsxmake.autoupdate")

-- targets
target(PLUGIN_NAME)
    add_deps("commonlibsse-ng")
    add_packages("nlohmann_json")

    add_rules("commonlibsse-ng.plugin", {
       name = PLUGIN_NAME,
       author = "nightlantern-mods",
       description = "Play Reversi and Liar's Dice with nearby NPCs on a Prisma UI board, with SkyrimNet commentary"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
    add_cxflags("/utf-8")

    -- dist/ holds the DLL and the view, laid out as they go into the mod
    after_build(function (target)
        local distdir = path.join(os.projectdir(), "dist")
        local viewdir = path.join(distdir, "PrismaUI", "views", VIEW_NAME)
        local plugindir = path.join(distdir, "SKSE", "Plugins")

        os.tryrm(distdir)
        os.mkdir(viewdir)
        os.mkdir(plugindir)

        os.cp(path.join(os.projectdir(), "view", "*"), viewdir)
        os.cp(target:targetfile(), plugindir)

        cprint("${bright green}distribution folder: ${clear}dist")
    end)
