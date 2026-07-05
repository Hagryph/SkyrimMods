Scriptname HagVampireBridge Hidden

Function TransformPlayer() Global
    PlayerVampireQuestScript vampireQuest = Game.GetFormFromFile(0x000EAFD5, "Dawnguard.esm") as PlayerVampireQuestScript
    if vampireQuest
        vampireQuest.VampireChange(Game.GetPlayer())
    endif
EndFunction
