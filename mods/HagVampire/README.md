# HagVampire

External HagLoader mod that adds a Vampire page with a transform button.

The button does not set the race directly. It calls the vanilla quest script function used by
Sanguinare Vampiris:

```text
PlayerVampireQuestScript.VampireChange(Game.GetPlayer())
```

The button uses HagLoader's queue-only Papyrus API to call a tiny bridge script:

```text
HagVampireBridge.TransformPlayer()
```

The bridge runs inside Papyrus and calls:

```text
PlayerVampireQuestScript.VampireChange(Game.GetPlayer())
```

That preserves the Papyrus path, including modded script behavior, while still letting Bethesda's
function handle the effects, race mapping, disease cleanup, vampire spells, `PlayerIsVampire`,
feeding timers, cure quest restart, and `SendVampirismStateChanged(true)`.

Build/deploy:

```powershell
.\build.ps1
```
