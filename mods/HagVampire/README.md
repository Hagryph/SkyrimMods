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

## Vampire stage effects

Skyrim still owns the four-stage thirst clock. At data load, HagVampire replaces the
base frost and Vampiric Drain magnitudes on the four player spells, from fed to
starving: 50/40/30/20% frost resistance and 5/4/3/2 Drain. Fire and sunlight are unchanged.

On Skyrim SE 1.6.1170, the Reanimate archetype dispatches to RVA `0x5CC230`.
The original checks corpse eligibility at `0x5C6CB0`, then compares the target's
level against the spell's magnitude at `0x5ABE20`. HagVampire keeps eligibility
but compares against the player's current level for Vampire's Servant only. It
also replaces the stage-one ash-pile effect's separate `GetLevel <= 6` condition
at the native condition evaluator (`0x4A05E0`). No new Papyrus calls or timer.

Build/deploy:

```powershell
.\build.ps1
```
