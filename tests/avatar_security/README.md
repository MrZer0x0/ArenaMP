# Player collision and automatic security regression tests

From the repository root run:

```sh
python3 tests/avatar_security/run_checks.py
python3 tests/v18/run_checks.py
```

The first runner extracts the real CharacterController security methods and native PickProbe branch and compiles them as C++17 against minimal engine test doubles. Tests exercise tool use, equipment/selection restoration, cancellations, broken/removed/restacked items and the player height helper. Integration guards check call sites and retained beast footwear restrictions. This does not replace a full engine build or in-game tests of meshes, animation keys, network responses and door clearance.
