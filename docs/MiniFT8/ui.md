# MiniFT8-V3 UI

## Vocabulary

```text
Mode      operating protocol: FT8 / FT4 / RTTY / CW / future
Screen    top-level UI location: RX / TX / O / S / V
Submenu   shallow group inside O, S, or V
Page      visible slice of a longer list when paging is later required
```

`Mode` is application state. It must not be reused to mean a UI screen.

## Logical geometry

The current MiniFT8 UI uses:

```text
30 columns x 8 rows
```

Layout:

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

The geometry is logical MiniFT8 UI state. The MiniShell adapter renders it through the text Display ABI.

## Input

Current logical controls:

```text
R / T / O / S / V    direct Screen selection
1..6                 activate visible main line
Up / Down             move selection
Left / Right          change supported values
Enter                 activate selected line
Esc or `              back/cancel
q / Q                 exit MiniFT8 to M$>
```

Physical keyboard, touch, buttons, BLE, or another input source must be normalized by MiniShell before MiniFT8 sees it.

## RX

Current RX uses prototype decode lines only. Real decoded FT8 output will later populate the same `UiModel` path.

```text
row 0   FT8 20m Default RX
row 1   1 <decoded line>
...
row 6   6 <decoded line>
row 7   R T O S V ...
```

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

Mode/Profile/Band are live prototype settings.

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

`Skip TX1` and `Max Retry` are currently live scheduler-owned values and persist to Station.txt.

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
2 Radio >
3 Band Profiles >
4 Logging >
5 Time / GPS >
6 System >
```

The submenus establish UI placement but most backend-dependent values remain `--` until their real contracts exist.

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

System Info intentionally reports portable concepts:

```text
Runtime: MiniShell
UI: text 30x8
```

It does not identify Linux or ncurses because those are below the MiniShell boundary.

## Rendering boundary

```text
UiModel
   -> ui_shell_render()
   -> UiFrame[30x8]
   -> minishell_ui_adapter
   -> MiniShell Display ABI
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
