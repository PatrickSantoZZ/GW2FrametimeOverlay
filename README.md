# GW2 Frametime Overlay (Nexus Addon)

A small, read-only overlay for **Guild Wars 2** that shows how your game is actually running and **what is holding your FPS back**.

![Nexus](https://img.shields.io/badge/Nexus-API%20v6-blue) ![C++](https://img.shields.io/badge/C%2B%2B-20-orange) ![License](https://img.shields.io/badge/License-MIT-green)

## Disclaimer
* AI was used to look over the code to make sure nothing is causing any issues 
* Modifying Guild Wars 2 through any third party software is not supported by ArenaNet nor by any of its partners.
* You are using this addon at your own risk.
* This addon only **reads** values. It does not patch, hook or change anything in the game.

## Features
* **FPS, frame time and 1% low** (99th percentile frame time over the last 5 seconds), updated twice per second.
* **Frame time graph** of the last 240 frames, so stutters are visible at a glance.
* **Bottleneck badge:**
  * **CPU** (blue): the busiest game thread is maxed out. Resolution won't help; character model limit/quality, reflections and shadows will.
  * **GPU** (red): the graphics card's 3D engine is maxed out. Lower resolution, render sampling, shadows, post-processing.
  * **LIM** (grey): neither is maxed. You're at your FPS cap, V-Sync or monitor refresh rate.
* Thresholds are adjustable in the options.
* Movable or locked (click-through), adjustable background opacity, optional "only in gameplay".
* Toggle keybind: **ALT+SHIFT+F** (rebindable in Nexus).

## How the detection works
* **GPU:** the Windows "GPU Engine" performance counter for the game's own process (the same source Task Manager uses).
* **CPU:** GW2 does most of its work on one main thread, so total CPU usage is misleading (8 cores at 15% can mean one thread at 100%). The overlay measures every game thread and looks at the **busiest single thread**.
* Both are sampled every 500 ms on a background thread and smoothed; a state has to hold for two samples before the badge changes.

## Installation
Copy `GW2FrametimeOverlay.dll` into `Guild Wars 2/addons/` and enable it in Nexus.
