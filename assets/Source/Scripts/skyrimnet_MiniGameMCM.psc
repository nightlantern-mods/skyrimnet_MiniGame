Scriptname skyrimnet_MiniGameMCM extends MCM_ConfigBase
{MCM Helper menu. Settings live in MCM/Settings/skyrimnet_MiniGame.ini; defaults in MCM/Config/skyrimnet_MiniGame/settings.ini.}

Event OnSettingChange(String a_ID)
	skyrimnet_MiniGamePlayerAlias playerAlias = GetAlias(0) as skyrimnet_MiniGamePlayerAlias
	If playerAlias
		playerAlias.Setup()
	EndIf
EndEvent
