# Player clearance and stair regression checks

On Linux with a C++17 compiler and Bullet/OSG development headers:

```sh
python3 tests/player_clearance/run_checks.py
```

Alternatively, use `--deps /path/to/extracted-debian-root` to point at development headers and libraries extracted from Debian packages. Dependencies are test prerequisites and are not bundled into the source archive.

The runner compiles the actual player convex-hull helper and production Stepper code. Bullet sweeps use a world-only trace callback and the same near-tangent contact tolerance as the game. Cases include independent shoulder/depth dimensions, flat feet, scaling and yaw, a narrow passage, retained wall/ceiling blocking, and 36 box/triangle-mesh ascent/descent combinations across eight 12-unit steps (scales 0.8/1.0/1.3 and yaw 0/45/90 degrees). It does not run the full engine, NPC contact handlers, network code or original Morrowind stair meshes.
