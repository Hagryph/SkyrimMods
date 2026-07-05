Scriptname HagVampireBridge

Function TransformPlayer() Global
    PlayerVampireQuestScript vampireQuest = ResolvePlayerVampireQuest()
    if vampireQuest
        vampireQuest.VampireChange(Game.GetPlayer())
    endif
EndFunction

Function CurePlayer() Global
    PlayerVampireQuestScript vampireQuest = ResolvePlayerVampireQuest()
    if vampireQuest
        vampireQuest.VampireCure(Game.GetPlayer())
    endif
EndFunction

PlayerVampireQuestScript Function ResolvePlayerVampireQuest() Global
    PlayerVampireQuestScript vampireQuest = Game.GetForm(0x000EAFD5) as PlayerVampireQuestScript
    if vampireQuest
        return vampireQuest
    endif

    vampireQuest = Game.GetFormFromFile(0x000EAFD5, "Skyrim.esm") as PlayerVampireQuestScript
    if vampireQuest
        return vampireQuest
    endif

    vampireQuest = Game.GetFormFromFile(0x000EAFD5, "Update.esm") as PlayerVampireQuestScript
    if vampireQuest
        return vampireQuest
    endif

    vampireQuest = Game.GetFormFromFile(0x000EAFD5, "Dawnguard.esm") as PlayerVampireQuestScript
    if vampireQuest
        return vampireQuest
    endif

    vampireQuest = Game.GetFormFromFile(0x000EAFD5, "HearthFires.esm") as PlayerVampireQuestScript
    if vampireQuest
        return vampireQuest
    endif

    return Game.GetFormFromFile(0x000EAFD5, "Dragonborn.esm") as PlayerVampireQuestScript
EndFunction
