Scriptname HagVampireBridge

Function TransformPlayer() Global
    Quest vampireQuestForm = Game.GetFormFromFile(0x000EAFD5, "Skyrim.esm") as Quest
    PlayerVampireQuestScript vampireQuest = vampireQuestForm as PlayerVampireQuestScript
    if vampireQuest
        vampireQuest.VampireChange(Game.GetPlayer())
    endif
EndFunction
