-- Server-owned ESM/ESP files, in EXACT client load order. Relative paths are
-- relative to the server process working directory. No client upload is trusted.
return {
    enabled = false,
    encoding = "win1251", -- win1250, win1251, win1252, utf8
    files = {
        -- "Data Files/Morrowind.esm",
        -- "Data Files/Tribunal.esm",
        -- "Data Files/Bloodmoon.esm",
        -- "Data Files/ArenaMP.esp",
    },
    -- Empty files: resolve this ordered server manifest in these directories.
    -- No Morrowind/DLC/custom plugin names are hardcoded by the API.
    manifest = "requiredDataFiles.json", -- relative to config.dataPath
    dataDirectories = { "Data Files" }, -- process-relative or absolute; last wins
    expectedKey = "", -- optional SHA-256 contentKey from Content.GetInfo()
    autoSnapshots = true,
    maxSnapshotsPerActor = 8,
    allowItemRecovery = false, -- explicit restoration of missing historical items
}
