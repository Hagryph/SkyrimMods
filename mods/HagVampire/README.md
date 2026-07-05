# HagVampire

External HagLoader mod that adds a Vampire page with a transform button.

The button does not set the race directly. It calls the vanilla quest script function used by
Sanguinare Vampiris:

```text
PlayerVampireQuestScript.VampireChange(Game.GetPlayer())
```

The mod does not use `cqf`. It uses HagLoader's queue-only loader API to advance the vanilla vampire
quest to its transformation stage and receive an optional async result callback:

```text
setstage PlayerVampireQuest 10
```

That function handles the transform effects, race mapping, disease cleanup, vampire spells,
`PlayerIsVampire`, feeding timers, cure quest restart, and `SendVampirismStateChanged(true)`.

Build/deploy:

```powershell
.\build.ps1
```
