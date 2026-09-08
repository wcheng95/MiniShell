# MiniFT8-V3 UI

## Vocabulary

```text
Mode      operating protocol: FT8 / FT4 / RTTY / CW / future
Screen    top-level UI location: RX / TX / O / S / V
Submenu   shallow group inside O, S, or V
Page      visible slice of a longer list when paging is required
Profile   MiniFT8 resource/presentation policy: ADV / DESKTOP
```

`Mode` is application state. It must not be reused to mean a UI screen or hardware platform.

## Logical geometry

Logical UI geometry is a **MiniFT8 profile choice**, not a universal MiniShell or MiniFT8 limit.

The inherited ADV profile uses:

```text
30 columns x 8 rows
```

with the familiar Cardputer-oriented layout:

```text
row 0   status / mode strip
row 1   main line 1
row 2   main line 2
row 3   main line 3
row 4   main line 4
row 5   main line 5
row 6   main line 6
row 7   contextual help
```

The DESKTOP profile may use a larger logical frame and more visible RX lines. Shared MiniFT8 logic must not branch on Linux/Cardputer identity to choose geometry; it consumes the active MiniFT8 profile and renders through the MiniShell Display API.

## Input

Current logical controls:

```text
R / T / O / S / V    direct Screen selection
1..6                 activate visible main line where the ADV layout uses six items
Up / Down            move selection
Left / Right          change supported values
Enter                 activate selected line
Esc or `              back/cancel
q / Q                 exit MiniFT8 to M$>
```

Physical keyboard, touch, buttons, BLE, or another input source must be normalized by MiniShell before MiniFT8 sees it.

## RX

The current implementation uses prototype decode lines only. Real decoded FT8 output will later populate the same `UiModel` path.

ADV profile presentation remains compatible with the inherited six-line behavior. DESKTOP may show more lines without changing RX/QSO semantics.

## TX

Current TX is a placeholder queue view. QSO/autoseq behavior is not yet integrated.

## Operation screen — O

Root:

```text
1 Mode: FT8
2 Profile: Default
3 Band: 20m
4 CQ / Beacon >
5 TX >
6 Message >
```

Mode/Profile/Band are live prototype settings. The `Profile` item shown here is the existing station/operating profile concept and should not be confused with the application presentation profiles `ADV` / `DESKTOP`; naming may be revisited when P1 formalizes MiniFT8 application profiles.

### O -> CQ / Beacon

```text
1 CQ Type: --
2 Beacon: --
```

### O -> TX

```text
1 Offset Source: --
2 Fixed Offset: --
3 Skip TX1: OFF
4 Max Retry: 3
5 Tune: --
```

`Skip TX1` and `Max Retry` are currently live scheduler-owned values and persist to `station.txt`.

### O -> Message

```text
1 Send FreeText >
2 Edit FreeText >
3 Current: (empty)
```

## Settings screen — S

Root:

```text
1 Station >
2 I/O Paths >
3 Band Profiles >
4 Logging >
5 Time / GPS >
6 System >
```

### S -> I/O Paths

The three station resources are intentionally independent:

```text
1 RX Audio: --
2 TX Audio: --
3 Control: --
```

There is no monolithic `Radio` selection at the MiniFT8 application boundary. A source/profile may associate these paths with the same physical device, but MiniFT8 configures and reasons about RX Audio, TX Audio, and Control independently.

The submenus establish UI placement but most backend-dependent values remain `--` until their real application wiring exists.

## View screen — V

V is read-only.

Root:

```text
1 Status >
2 GPS >
3 QSO / Log >
4 Performance >
5 System Info >
6 About >
```

The Status view uses the same resource vocabulary:

```text
RX Audio: --
TX Audio: --
Control: --
```

System Info intentionally reports portable concepts rather than encouraging application branching on backend identity.

## Rendering boundary

```text
UiModel
   -> ui_shell_render()
   -> profile-sized UiFrame
   -> minishell_ui_adapter
   -> MiniShell Display API
```

Input is the reverse boundary:

```text
MiniShell key event
   -> minishell_ui_adapter
   -> UiInput
   -> ui_shell_handle_input()
   -> AppAction
```

`ui_shell` must remain independently unit-testable without a terminal or hardware display.
