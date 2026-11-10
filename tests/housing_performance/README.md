# Housing topology regression test

Run from the repository root:

```sh
luajit tests/housing_performance/topology.lua
```

An optional first argument selects a baseline houseHelper.lua. Compare stdout from both runs; stderr contains timing. The fixture has 1740 properties. It exercises case-insensitive linked-room lookup, Cyrillic names, missing cells, live permissions, disabling, room linking, removal and registration. SendHomeState formatting and native transport are stubbed. This is not a live-server benchmark.
