# HagVampire

External HagLoader mod that adds a Vampire page with a button to run the vanilla vampire conversion
protocol.

The button does not set the race directly. It calls the vanilla quest script function used by
Sanguinare Vampiris:

```text
PlayerVampireQuestScript.VampireChange(Game.GetPlayer())
```

Implementation uses HagLoader's exported console script execution service, which runs the game's own
Script `CompileAndRun` path with:

```text
cqf PlayerVampireQuest VampireChange player
```

That function handles the transform effects, race mapping, disease cleanup, vampire spells,
`PlayerIsVampire`, feeding timers, cure quest restart, and `SendVampirismStateChanged(true)`.

Build/deploy:

```powershell
.\build.ps1
```
