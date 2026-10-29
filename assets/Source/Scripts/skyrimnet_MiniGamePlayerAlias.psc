Scriptname skyrimnet_MiniGamePlayerAlias extends ReferenceAlias
{Board hotkey. Settings come from the MCM (MCM Helper): MCM/Settings/skyrimnet_MiniGame.ini.}

String Property ModName = "skyrimnet_MiniGame" AutoReadOnly

Int registeredKey = -1

Event OnInit()
	Setup()
EndEvent

Event OnPlayerLoadGame()
	Setup()
EndEvent

; (re)reads the MCM settings; the MCM calls it whenever a setting changes
Function Setup()
	skyrimnet_MiniGameNative.ReloadSettings()
	Int hotkey = MCM.GetModSettingInt(ModName, "iOpenKey:General")

	UnregisterForAllKeys()
	registeredKey = hotkey
	If hotkey > 0
		RegisterForKey(hotkey)
	EndIf
EndFunction

Event OnKeyDown(Int keyCode)
	If keyCode != registeredKey || UI.IsTextInputEnabled()
		Return
	EndIf
	; Utility.IsInMenuMode() is true while the board is open, so it is not checked here:
	; the key also closes the board. The plugin ignores it while a game menu is up.
	skyrimnet_MiniGameNative.ToggleBoard()
EndEvent
