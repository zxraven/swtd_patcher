# swtd_patcher

A small standalone patcher for **Still Wakes the Deep** (Unreal Engine 5.4.3) that makes the game usable on an ultrawide monitor at native resolution **with DLSS Frame Generation enabled**, so that [OptiScaler](https://github.com/optiscaler/OptiScaler) can then raise the Frame Generation multiplier (for example to 4x).

The patcher does not ship any game files. It modifies your own copy of the game executable, keeps a backup and refuses to touch the file if anything does not match.

## The problem

The setup this was built for: a 5120x2160 (21:9) monitor, an NVIDIA RTX GPU, DLSS + DLSS Frame Generation in the game, and OptiScaler on top to get multi frame generation.

Three separate issues get in the way:

1. **The game does not offer the native resolution in exclusive Fullscreen.** The resolution list in Fullscreen mode tops out at 3440x1440. Writing `ResolutionSizeX=5120` / `ResolutionSizeY=2160` into `GameUserSettings.ini` by hand does not help: in exclusive Fullscreen the output still ends up pillarboxed, with black bars on both sides **everywhere**, including the main menu. This is not a camera or FOV problem, so camera fixes (including other ultrawide fixes for this game) change nothing in this mode. The likely cause is that the game switches the display to a mode from its own list rather than the native one.

2. **Borderless ("Windowed Fullscreen") removes the bars, but the game blocks DLSS Frame Generation there.** In any window mode other than exclusive Fullscreen the FG option is greyed out with *"This feature only works if 'Window Mode' is set to 'Fullscreen'"*. NVIDIA DLSS-G itself works fine in borderless mode on DX12; the restriction is purely in the game's own settings code, and it is enforced in two places:
   - the settings menu disables the option;
   - when settings are applied, the game silently forces the DLSS-G mode to *Off* if `FullscreenMode != Fullscreen`, even if FG was enabled in the ini.

3. **Cutscene cameras force 16:9.** Cinematic cameras have `bConstrainAspectRatio` set, so even at a proper 21:9 output, cutscenes and some interactions are pillarboxed to 16:9.

## Why this matters for OptiScaler

OptiScaler does not create DLSS Frame Generation out of nothing in this setup. It hooks the game's existing NVIDIA Streamline DLSS-G integration and overrides the multiplier (2x → 3x/4x, depending on GPU and OptiScaler/Streamline versions). That only works while the **game itself has DLSS-G 2x active**.

Before the patch you had to choose:

| Mode | Black bars | Game DLSS FG | OptiScaler MFG |
|---|---|---|---|
| Exclusive Fullscreen, 5120x2160 forced in ini | yes, everywhere | works | works, but pillarboxed |
| Borderless, 5120x2160 | no | **blocked by the game** | impossible |
| **Borderless + this patch** | **no** | **works** | **works** |

## What the patcher changes

Three small code patches in the game executable. Each one is located by a byte signature (with wildcards for addresses that change between builds), and each signature must match **exactly once**, otherwise nothing is written.

| # | Purpose | Location (reference build) | Change |
|---|---|---|---|
| 1 | Remove 16:9 pillarbox from cutscene cameras | `FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle`, branch on `bConstrainAspectRatio` (`+0x60`, bit 0) | `je` → `jmp`: always take the unconstrained path. Projection is then built from the real viewport and the axis constraint, so the image is not stretched. |
| 2 | Let DLSS-G actually turn on outside exclusive Fullscreen | Habitat settings apply code, right before `SetDLSSGMode` | `cmove ebx, eax` → `mov ebx, eax`: the requested FG mode is no longer replaced with *Off* when `UGameUserSettings::FullscreenMode` (`+0xA0`) is not 0. |
| 3 | Un-grey the FG option in the menu | Habitat settings UI condition for NVIDIA DLSS FG (anchored on the `cmp bl, 3` = *NVidia DLSS* check) | `je` → `jmp`: skip adding the "only works in Fullscreen" disable reason. The Intel XeFG and VSync conditions are left untouched. |

Reference build: the Steam executable with SHA-256
`841b30af1d0f71221be1cf24ce61ac49bd5a91d540c8272f670be3f9b5e763a8`.
Patched result: `0566e58b3daefe0fa23f12ac8e60f30eaf85d4b5800ac0575268a60d62f7fcc4`.

All offsets were found by static analysis of that build (UE reflection data gives the struct field offsets; the functions were identified from their code and call graph). Other builds may still match the signatures; if they do not, the patcher reports it and leaves the file alone.

## Usage

1. Close the game.
2. Put `swtd_patcher.exe` into the game folder next to `StillWakesTheDeep.exe` and run it. Alternatively, drag the game exe onto the patcher. It also looks for `Habitat\Binaries\Win64\Habitat-Win64-Shipping.exe`.
3. Check the report. Each patch is shown as *will be applied*, *already applied* or *NOT FOUND*.
4. A backup `StillWakesTheDeep.exe.bak` is created on first run and never overwritten afterwards.

Restore the original:

```
swtd_patcher.exe --restore
```

Running the patcher again on an already patched file does nothing. A Steam update replaces the executable, so just run the patcher again afterwards.

### Game settings

Edit `%LOCALAPPDATA%\Habitat\Saved\Config\Windows\GameUserSettings.ini` (section `[/Script/Habitat.HabitatLocalSettings]`):

```ini
ResolutionSizeX=5120
ResolutionSizeY=2160
LastUserConfirmedResolutionSizeX=5120
LastUserConfirmedResolutionSizeY=2160
FullscreenMode=1
LastConfirmedFullscreenMode=1
```

Use your monitor's native resolution. `FullscreenMode=1` is *Windowed Fullscreen* (borderless). Then start the game, select **NVIDIA DLSS** as the upscaler and enable **Frame Generation** in the graphics menu.

Requirements for DLSS-G itself still apply: an RTX GPU that supports it, a recent driver and *Hardware-accelerated GPU scheduling* enabled in Windows.

### OptiScaler

Install and configure OptiScaler for the game as usual (the game's binaries folder is `Habitat\Binaries\Win64`). With the patch, the game runs its own DLSS-G 2x in borderless mode, and OptiScaler can override the Frame Generation multiplier from there. Which multipliers are available depends on your GPU and on the OptiScaler and Streamline versions you use; refer to the OptiScaler documentation for the exact settings.

## Building

Cross-compile with MinGW-w64:

```
make                 # builds swtd_patcher.exe (static, no dependencies)
make swtd_test       # native build to test the patch logic on Linux
```

Or directly:

```
x86_64-w64-mingw32-gcc -O2 -municode -static -s -o swtd_patcher.exe src/swtd_patcher.c
```

The native test build was used to verify that patching the reference executable produces exactly the expected SHA-256 above, that a second run changes nothing, that `--restore` restores the original, and that an unrelated executable is rejected without modification.

## Notes

- Single-player game, no anti-cheat involved. Still, you modify the executable at your own risk; keep the backup.
- Windows SmartScreen or antivirus software may warn about an unsigned tool that modifies an exe. The full source is in `src/`; build it yourself if you prefer.
- Only patch 1 affects the image; patches 2 and 3 only remove the game's Fullscreen-only restriction for DLSS FG.
- If you only want to remove cutscene pillarboxing and do not use Frame Generation, RoseTheFlower's [UltrawideIndex](https://github.com/RoseTheFlower/UltrawideIndex) fix for this game is an alternative for that part.
