# MiniFT8-V3 UI

## Vocabulary

```text
Protocol       fixed application identity: FT8 for the `ft8` app
Screen         top-level UI location: RX / TX / O / S / V
Submenu        shallow group inside O, S, or V
Page           visible slice of a longer list when paging is required
Presentation   MiniFT8 UI/resource presentation: ADV / DESKTOP
Station Profile
               operating-profile setting shown on O as Default/User/etc.
```

Protocol selection is not MiniFT8 application state. Switching from FT8 to another protocol means leaving `ft8` and launching another MiniShell application such as future `ft4`, `cw`, `rtty`, or `js8`.

`Presentation` is also separate from MiniShell backend identity. Linux can run either MiniFT8 presentation, which is the basis of the P1 cross-profile test.

## P1 presentation profiles

P1 defines two application-level presentations:

```text
DESKTOP   30 columns x 8 rows
ADV       20 columns x 7 rows
```

Both use the same MiniFT8 controller, `UiModel`, navigation, actions, and screen definitions.

### DESKTOP

```text
row 0   status / protocol strip
row 1   main line 1
row 2   main line 2
row 3   main line 3
row 4   main line 4
row 5   main line 5
row 6   main line 6
row 7   contextual help/footer
```

### ADV

```text
row 0   compact status / protocol strip
row 1   main line 1
row 2   main line 2
row 3   main line 3
row 4   main line 4
row 5   main line 5
row 6   main line 6
```

ADV deliberately omits the footer rather than reducing the six main lines. This exactly fits the MiniShell Cardputer ADV Display surface of 20x7.

Presentation selection is launch policy, not persisted station state:

```text
M$> ft8                    # default: DESKTOP
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

`station.txt` does not contain a `presentation=` key.

The application does not inspect `platform=linux` or `platform=adv` to choose presentation. P2 will arrange for the compiled-in ADV `ft8` entry to launch the ADV presentation explicitly.

## Input

Current logical controls:

```text
R / T / O / S / V    direct Screen selection
1..6                 activate visible main line
Up / Down            move selection
Left / Right          change supported values
Enter                 activate selected line
Esc or `              back/cancel
q / Q                 exit `ft8` to M$>
```

Physical keyboard, touch, buttons, BLE, or another input source must be normalized by MiniShell before MiniFT8 sees it.

## RX

The current implementation uses prototype decode lines only. Real decoded FT8 output will later populate the same `UiModel` path.

Both P1 presentations retain six visible RX lines. A future DESKTOP enhancement may expose more data, but P1 intentionally keeps content semantics identical so profile/backend comparisons are easy to reason about.

## TX

Current TX is a placeholder queue view. QSO/autoseq behavior is not yet integrated.

## Operation screen — O

Root:

```text
1 Protocol: FT8
2 Profile: Default
3 Band: 20m
4 CQ / Beacon >
5 TX >
6 Message >
```

`Protocol: FT8` is informational and not changeable inside the app.

The `Profile` item here is the **station/operating profile**. It is intentionally not the ADV/DESKTOP application presentation.

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

The Status view reports fixed `Protocol: FT8` plus the same resource vocabulary:

```text
RX Audio: --
TX Audio: --
Control: --
```

System Info reports the active application presentation explicitly:

```text
Runtime: MiniShell
Presentation: ADV       # or DESKTOP
UI: text 20x7           # or 30x8
App: ft8
Station: Default
Band: 20m
```

This remains application policy; it does not expose or branch on MiniShell backend identity.

## Rendering boundary

```text
ft8 presentation profile
   -> geometry/footer policy
   -> UiModel
   -> ui_shell_render()
   -> profile-sized UiFrame
   -> ft8_ui_adapter
   -> MiniShell Display API
```

Input is the reverse boundary:

```text
MiniShell key event
   -> ft8_ui_adapter
   -> UiInput
   -> ui_shell_handle_input()
   -> AppAction
```

`ui_shell` remains independently unit-testable without a terminal or hardware display. The same smoke test renders both DESKTOP 30x8 and ADV 20x7 frames.
