# HagVampire

External HagLoader mod that adds a Vampire page with a transform button.

The button does not set the race directly. It calls the vanilla quest script function used by
Sanguinare Vampiris:

```text
PlayerVampireQuestScript.VampireChange(Game.GetPlayer())
```

The mod uses HagLoader's queue-only loader API to run the vanilla quest script function and receive
an optional async result callback:

```text
cqf PlayerVampireQuest VampireChange player
```

That preserves the Papyrus path, including any additional scripts or modded behavior attached to
the vampire quest, while still letting Bethesda's function handle the effects, race mapping,
disease cleanup, vampire spells, `PlayerIsVampire`, feeding timers, cure quest restart, and
`SendVampirismStateChanged(true)`.

Build/deploy:

```powershell
.\build.ps1
```
