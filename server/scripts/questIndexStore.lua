-- ArenaMP X013 - server-owned quest item index.
--
-- The classification of "which item records are quest sources" can only be
-- derived from the loaded ESM/ESP set, which the server does not parse. X012
-- therefore trusted a per-item flag inside container packets, which meant a
-- modified client could declare anything a quest item and mint infinite copies
-- out of any container.
--
-- X013 keeps the derivation on the client but moves the authority here. Clients
-- act as oracles: they upload the derived index, this module rehashes the
-- payload, requires agreement from independent uploads (or a single staff
-- upload), stores the result, and from then on answers every classification
-- question itself. Until an index has been accepted, phasing is off and the
-- server behaves exactly like vanilla.

local questIndexStore = {}

local STAGE_HANDSHAKE = 1
local STAGE_CHUNK = 2
local STAGE_END = 3

local MODE_OFF = 0
local MODE_VERIFY = 1
local MODE_UPLOAD = 2

local STORE_PATH = "custom/questIndex.json"
local INDEX_SCHEMA_VERSION = 2 -- X058: direct-world quest items with strong evidence are phaseable

local state = {
    loaded = false,
    trusted = false,
    contentKey = nil,
    indexHash = nil,
    entries = {},
    entryCount = 0,
    startupRefreshPending = false,
    storedContentKey = nil,
    storedIndexHash = nil
}

local pending = {}
local confirmations = {}
local requested = {}
local driftWarned = {}

local function lower(value)
    if value == nil then return "" end
    return string.lower(tostring(value))
end

local function configNumber(name, fallback)
    if config ~= nil and type(config[name]) == "number" then
        return config[name]
    end
    return fallback
end

local function configBool(name, fallback)
    if config ~= nil and type(config[name]) == "boolean" then
        return config[name]
    end
    return fallback
end

-- X020: autogeneration is now the hard bootstrap default. Older X013-X018
-- instructions sometimes left config.questIndexAutoGenerate=false in an
-- existing config.lua. That stale value silently forced quorum mode and left
-- phasing disabled long enough for the first player to remove a quest source
-- globally. We intentionally ignore that legacy switch. Administrators who
-- explicitly want the old two-client/staff approval flow must opt in with the
-- new unambiguous flag below.
local function requireQuorumBootstrap()
    return configBool("questIndexRequireQuorum", false)
end

local function autoGenerateEnabled()
    return not requireQuorumBootstrap()
end

-- Dual modular rolling hash, mirrored byte for byte from
-- apps/openmw/mwmp/QuestItemIndex.cpp (dualHash). Both moduli are below 2^31 so
-- every intermediate product stays exactly representable as a Lua number; do not
-- swap this for a bitop hash without changing the C++ side in the same commit.
function questIndexStore.Hash(value)
    local h1, h2 = 0, 0
    for i = 1, #value do
        local b = string.byte(value, i)
        h1 = (h1 * 131 + b) % 2147483647
        h2 = (h2 * 137 + b) % 2147483629
    end
    return string.format("%08x%08x", h1, h2)
end

local function hashEntries(entries)
    -- The client sends entries already sorted with a plain byte comparison and
    -- the server hashes them in arrival order, so no locale-dependent sort has
    -- to be reproduced here.
    return questIndexStore.Hash(table.concat(entries, "\n"))
end

local function applyIndex(contentKey, indexHash, entries)
    state.contentKey = contentKey
    state.indexHash = indexHash
    state.entries = {}
    state.entryCount = 0

    for _, refId in ipairs(entries) do
        local id = lower(refId)
        if id ~= "" and state.entries[id] == nil then
            state.entries[id] = true
            state.entryCount = state.entryCount + 1
        end
    end

    state.trusted = true
end

local function load()
    if state.loaded then return end
    state.loaded = true

    if not tes3mp.DoesFileExist(tes3mp.GetModDir() .. "/" .. STORE_PATH) then
        if autoGenerateEnabled() then
            if config ~= nil and config.questIndexAutoGenerate == false then
                tes3mp.LogMessage(enumerations.log.WARN,
                    "[ArenaMP Core] Ignoring legacy config.questIndexAutoGenerate=false; X020 auto-bootstrap is active. Use config.questIndexRequireQuorum=true only if quorum mode is intentionally required")
            end
            tes3mp.LogMessage(enumerations.log.INFO,
                "[ArenaMP Core] No stored index; X020 auto-bootstrap will accept the first valid client upload and create questIndex.json")
        else
            tes3mp.LogMessage(enumerations.log.INFO,
                "[ArenaMP Core] No stored index; explicit questIndexRequireQuorum=true keeps phasing disabled until quorum/staff approval")
        end
        return
    end

    local ok, data = pcall(jsonInterface.load, STORE_PATH)
    if not ok or type(data) ~= "table" or type(data.entries) ~= "table" or
        type(data.contentKey) ~= "string" or type(data.indexHash) ~= "string" then
        tes3mp.LogMessage(enumerations.log.ERROR,
            "[ArenaMP Core] " .. STORE_PATH .. " is unreadable; phasing stays disabled")
        return
    end

    -- X058: the classifier changed. Reusing an older accepted index would keep
    -- already-missed direct-world quest items shared forever, even after updating
    -- the executable. Invalidate only the classification schema, then let the
    -- normal first-valid-client bootstrap rebuild questIndex.json automatically.
    if tonumber(data.schemaVersion) ~= INDEX_SCHEMA_VERSION then
        tes3mp.LogMessage(enumerations.log.WARN,
            "[ArenaMP Core] " .. STORE_PATH .. " uses quest-index schema " ..
            tostring(data.schemaVersion or 1) .. "; rebuilding schema " .. INDEX_SCHEMA_VERSION ..
            " before quest item phasing becomes active")
        return
    end

    -- Re-verify on load. A hand-edited or truncated store must not silently
    -- become authoritative just because it sits in the right place.
    local recomputed = hashEntries(data.entries)
    if recomputed ~= data.indexHash then
        tes3mp.LogMessage(enumerations.log.ERROR,
            "[ArenaMP Core] Stored index hash mismatch (" .. recomputed .. " vs " .. data.indexHash ..
            "); phasing stays disabled until a fresh index is accepted")
        return
    end

    -- X046: a valid persisted index is authoritative across ordinary server
    -- restarts. Do NOT force every first client after a restart to rescan all
    -- ESM/ESP CELL contexts. Apart from making login needlessly expensive, that
    -- raw scan proved unsafe with some large content sets (TR/Cyr/Sky) and could
    -- terminate the client while logging in.
    --
    -- If the file is absent/unreadable/hash-invalid, state.trusted remains false
    -- and OnPlayerReady() requests a fresh MODE_UPLOAD, which recreates this same
    -- file. Manual Reset() remains the explicit admin rebuild path.
    if configBool("questIndexRefreshOnServerStart", false) then
        tes3mp.LogMessage(enumerations.log.WARN,
            "[ArenaMP Core] questIndexRefreshOnServerStart is deprecated and ignored by X046; the valid stored questIndex.json will be reused. Use the explicit quest index reset/rebuild command when the content set intentionally changes.")
    end

    applyIndex(data.contentKey, data.indexHash, data.entries)
    state.startupRefreshPending = false
    state.storedContentKey = nil
    state.storedIndexHash = nil

    tes3mp.LogMessage(enumerations.log.INFO,
        "[ArenaMP Core] Reusing persistent index: loaded " .. state.entryCount ..
        " phaseable records for content key " .. state.contentKey ..
        " (hash " .. state.indexHash .. "); no startup client scan required")
end

local function commit(contentKey, indexHash, entries, reason, generatedBy, generationMode)
    applyIndex(contentKey, indexHash, entries)
    state.startupRefreshPending = false
    state.storedContentKey = nil
    state.storedIndexHash = nil

    jsonInterface.save(STORE_PATH, {
        schemaVersion = INDEX_SCHEMA_VERSION,
        contentKey = contentKey,
        indexHash = indexHash,
        entryCount = #entries,
        entries = entries,
        generatedAt = os.time(),
        generatedBy = generatedBy or "server",
        generationMode = generationMode or "confirmed"
    })

    confirmations = {}

    tes3mp.LogMessage(enumerations.log.INFO,
        "[ArenaMP Core] Accepted index with " .. state.entryCount .. " records (" .. reason ..
        "); quest item phasing is now active")

    -- Everyone online can stop classifying locally now.
    for pid, _ in pairs(Players) do
        if Players[pid] ~= nil and Players[pid]:IsLoggedIn() then
            requested[pid] = MODE_OFF
            tes3mp.SendQuestIndexRequest(pid, MODE_OFF)
        end
    end
end

--- Is the server allowed to phase anything at all?
function questIndexStore.IsTrusted()
    load()
    return state.trusted
end

--- Authoritative classification. Never consults the packet the client sent.
function questIndexStore.IsQuestItem(refId)
    load()
    if not state.trusted or refId == nil then return false end
    return state.entries[lower(refId)] == true
end

function questIndexStore.GetStatus()
    load()
    return {
        trusted = state.trusted,
        schemaVersion = INDEX_SCHEMA_VERSION,
        contentKey = state.contentKey,
        indexHash = state.indexHash,
        entryCount = state.entryCount,
        autoGenerate = autoGenerateEnabled(),
        requireQuorum = requireQuorumBootstrap(),
        startupRefreshPending = state.startupRefreshPending,
        storedContentKey = state.storedContentKey,
        storedIndexHash = state.storedIndexHash,
        pendingConfirmations = confirmations
    }
end

--- Drop the accepted index and start collecting again. Use after a content change
--- that the server should re-learn, or from an admin command.
function questIndexStore.Reset()
    load()
    state.trusted = false
    state.contentKey = nil
    state.indexHash = nil
    state.entries = {}
    state.entryCount = 0
    state.startupRefreshPending = false
    state.storedContentKey = nil
    state.storedIndexHash = nil
    pending = {}
    confirmations = {}
    driftWarned = {}

    tes3mp.LogMessage(enumerations.log.WARN,
        "[ArenaMP Core] Index reset; phasing is temporarily disabled while a fresh index is requested")

    for pid, _ in pairs(Players) do
        if Players[pid] ~= nil and Players[pid]:IsLoggedIn() then
            questIndexStore.RequestFrom(pid, MODE_UPLOAD)
        end
    end
end

function questIndexStore.RequestFrom(pid, mode)
    if Players[pid] == nil then return false end

    -- X015 safety net: if server/scripts from a newer ArenaMP build are ever
    -- copied over an older server binary, do not crash the whole server.
    -- Quest item phasing remains fail-closed until the native QuestIndex API
    -- is available in the matching executable.
    if type(tes3mp.SendQuestIndexRequest) ~= "function" then
        tes3mp.LogMessage(enumerations.log.ERROR,
            "[ArenaMP Core] Native QuestIndex API is unavailable; phasing remains disabled. Rebuild server/client from the same ArenaMP cumulative patch.")
        return false
    end

    requested[pid] = mode
    pending[pid] = nil
    tes3mp.SendQuestIndexRequest(pid, mode)
    return true
end

--- X046: validate/load the persistent store during server startup. A valid
--- questIndex.json is reused as-is. Only a missing/invalid store leaves the
--- system untrusted and causes the first fully logged-in client to create one.
function questIndexStore.OnServerStart()
    load()
end

--- Called once per player as soon as they are fully logged in.
function questIndexStore.OnPlayerReady(pid)
    load()

    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    if requested[pid] ~= nil then return end

    if not state.trusted then
        questIndexStore.RequestFrom(pid, MODE_UPLOAD)
    elseif configBool("questIndexVerifyOnLogin", false) then
        questIndexStore.RequestFrom(pid, MODE_VERIFY)
    else
        -- Nothing to do. Telling the client explicitly keeps its own classifier
        -- switched off, so an ordinary session never scans the ESM files.
        questIndexStore.RequestFrom(pid, MODE_OFF)
    end
end

function questIndexStore.OnPlayerLeave(pid)
    pending[pid] = nil
    requested[pid] = nil
    driftWarned[pid] = nil
end

local function noteDrift(pid, contentKey, indexHash)
    if driftWarned[pid] then return end
    driftWarned[pid] = true

    tes3mp.LogMessage(enumerations.log.WARN,
        "[ArenaMP Core] " .. logicHandler.GetChatName(pid) .. " reports content key " .. contentKey ..
        " / hash " .. indexHash .. ", server has " .. tostring(state.contentKey) .. " / " ..
        tostring(state.indexHash) .. " - their load order differs from the one the index was built from")
end

local function registerConfirmation(pid, contentKey, indexHash, entries)
    -- Another upload may have completed while this player was still sending
    -- chunks. Once an index becomes authoritative, a late upload must never
    -- replace it. It is only useful as a drift signal.
    if state.trusted then
        if contentKey ~= state.contentKey or indexHash ~= state.indexHash then
            noteDrift(pid, contentKey, indexHash)
        end
        return
    end

    local key = contentKey .. "|" .. indexHash
    local accountName = lower(Players[pid].accountName)
    local chatName = logicHandler.GetChatName(pid)

    -- X019: zero-touch bootstrap. The dedicated server cannot derive quest
    -- references from ESM/ESP itself, so the first fully logged-in matching
    -- client acts as the build worker. The server still recomputes the payload
    -- hash before reaching this function, then immediately persists the result
    -- as server/data/custom/questIndex.json. The legacy questIndexAutoGenerate flag is ignored in X020; set
    -- questIndexRequireQuorum=true to restore the older staff/quorum approval
    -- flow explicitly.
    if autoGenerateEnabled() then
        tes3mp.LogMessage(enumerations.log.INFO,
            "[ArenaMP Core] X020 auto-bootstrap: accepting first verified upload from " .. chatName ..
            " (" .. tostring(#entries) .. " records, hash " .. indexHash .. ")")
        commit(contentKey, indexHash, entries,
            "auto-generated from first valid upload by " .. chatName,
            accountName, "automatic-first-valid-client-x020")
        return
    end

    if Players[pid]:IsServerStaff() then
        commit(contentKey, indexHash, entries, "uploaded by staff " .. chatName,
            accountName, "staff")
        return
    end

    local record = confirmations[key]
    if record == nil then
        record = { names = {}, count = 0, entries = entries }
        confirmations[key] = record
    end

    if record.names[accountName] == nil then
        record.names[accountName] = true
        record.count = record.count + 1
    end

    local required = math.max(1, configNumber("questIndexRequiredConfirmations", 2))

    tes3mp.LogMessage(enumerations.log.INFO,
        "[ArenaMP Core] Upload from " .. logicHandler.GetChatName(pid) .. " accepted as confirmation " ..
        record.count .. "/" .. required .. " for hash " .. indexHash)

    if record.count >= required then
        commit(contentKey, indexHash, record.entries,
            record.count .. " independent confirmations", accountName, "quorum")
    end
end

--- Entry point for the ID_PLAYER_QUEST_INDEX callback.
function questIndexStore.OnPacket(pid)
    load()

    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    local stage = tes3mp.GetQuestIndexStage(pid)
    local contentKey = tes3mp.GetQuestIndexContentKey(pid)
    local indexHash = tes3mp.GetQuestIndexHash(pid)

    if #contentKey ~= 16 or #indexHash ~= 16 then
        tes3mp.LogMessage(enumerations.log.WARN,
            "[ArenaMP Core] Discarding malformed keys from " .. logicHandler.GetChatName(pid))
        pending[pid] = nil
        return
    end

    if stage == STAGE_HANDSHAKE then
        local entryCount = tes3mp.GetQuestIndexEntryCount(pid)

        if state.trusted then
            -- The server already classifies on its own; a handshake is only used
            -- to notice that this player's content differs from what the index
            -- was built from. It changes nothing about phasing.
            if contentKey ~= state.contentKey or indexHash ~= state.indexHash then
                noteDrift(pid, contentKey, indexHash)
            end
            pending[pid] = nil
            return
        end

        if requested[pid] ~= MODE_UPLOAD then
            -- Unsolicited upload attempt. Ignore it rather than letting a client
            -- push an index at a moment of its own choosing.
            pending[pid] = nil
            return
        end

        pending[pid] = {
            contentKey = contentKey,
            indexHash = indexHash,
            entryCount = entryCount,
            entries = {},
            chunks = 0
        }

    elseif stage == STAGE_CHUNK then
        local buffer = pending[pid]
        if buffer == nil then return end

        if buffer.contentKey ~= contentKey or buffer.indexHash ~= indexHash then
            tes3mp.LogMessage(enumerations.log.WARN,
                "[ArenaMP Core] " .. logicHandler.GetChatName(pid) .. " changed keys mid-upload; discarding")
            pending[pid] = nil
            return
        end

        local size = tes3mp.GetQuestIndexChunkSize(pid)
        for i = 0, size - 1 do
            if #buffer.entries >= buffer.entryCount then
                tes3mp.LogMessage(enumerations.log.WARN,
                    "[ArenaMP Core] " .. logicHandler.GetChatName(pid) .. " sent more entries than declared; discarding")
                pending[pid] = nil
                return
            end
            table.insert(buffer.entries, lower(tes3mp.GetQuestIndexChunkEntry(pid, i)))
        end

        buffer.chunks = buffer.chunks + 1

    elseif stage == STAGE_END then
        local buffer = pending[pid]
        pending[pid] = nil
        if buffer == nil then return end

        if #buffer.entries ~= buffer.entryCount then
            tes3mp.LogMessage(enumerations.log.WARN,
                "[ArenaMP Core] Incomplete upload from " .. logicHandler.GetChatName(pid) ..
                " (" .. #buffer.entries .. "/" .. buffer.entryCount .. "); discarded")
            return
        end

        -- The declared hash is worthless on its own: recompute it over what
        -- actually arrived. This is what stops a client from announcing a
        -- popular hash while shipping a payload of its own choosing.
        local recomputed = hashEntries(buffer.entries)
        if recomputed ~= buffer.indexHash then
            tes3mp.LogMessage(enumerations.log.WARN,
                "[ArenaMP Core] Hash mismatch from " .. logicHandler.GetChatName(pid) ..
                " (" .. recomputed .. " vs " .. buffer.indexHash .. "); discarded")
            return
        end

        registerConfirmation(pid, buffer.contentKey, buffer.indexHash, buffer.entries)
        requested[pid] = MODE_OFF
    end
end

return questIndexStore
