-- ArenaMP Windows io2 crash avoidance. Lua module resolution selects this
-- compatibility layer before the legacy C module (server/lib/io2.dll).
-- The native DLL is intentionally not loaded: an access violation cannot be
-- handled by Lua pcall. All standard file methods remain normal Lua handles.
return require("arenaSafeIo")
