local customEventHooks = {}

customEventHooks.validators = {}
customEventHooks.handlers = {}

function customEventHooks.makeEventStatus(validDefaultHandler, validCustomHandlers)
    return {
        validDefaultHandler = validDefaultHandler,
        validCustomHandlers = validCustomHandlers
    }
end

function customEventHooks.updateEventStatus(oldStatus, newStatus)
    if newStatus == nil then
        return oldStatus
    end
    local result = {}
    if newStatus.validDefaultHandler ~= nil then
        result.validDefaultHandler = newStatus.validDefaultHandler
    else
        result.validDefaultHandler = oldStatus.validDefaultHandler
    end
    
    if newStatus.validCustomHandlers ~= nil then
        result.validCustomHandlers = newStatus.validCustomHandlers
    else
        result.validCustomHandlers = oldStatus.validCustomHandlers
    end
    
    return result
end

function customEventHooks.registerValidator(event, callback)
    if customEventHooks.validators[event] == nil then
        customEventHooks.validators[event] = {}
    end
    table.insert(customEventHooks.validators[event], callback)
end

function customEventHooks.registerHandler(event, callback)
    if customEventHooks.handlers[event] == nil then
        customEventHooks.handlers[event] = {}
    end
    table.insert(customEventHooks.handlers[event], callback)
end

function customEventHooks.triggerValidators(event, args)
    local eventStatus = customEventHooks.makeEventStatus(true, true)
    if customEventHooks.validators[event] ~= nil then
        for _, callback in ipairs(customEventHooks.validators[event]) do
            eventStatus = customEventHooks.updateEventStatus(eventStatus, callback(eventStatus, unpack(args)))
        end
    end
    return eventStatus
end

function customEventHooks.triggerHandlers(event, eventStatus, args)
    if customEventHooks.handlers[event] ~= nil then
        for _, callback in ipairs(customEventHooks.handlers[event]) do
             -- Y110: attribute expensive login hooks without changing their order
             -- or suppressing failures. require is lazy to avoid a module cycle.
             local loginEvent = event == "OnPlayerFinishLogin" or event == "OnPlayerAuthentified"
                 or event == "OnPlayerConnect"
             local timing = loginEvent and require("loginSyncHelper") or nil
             local started = timing and timing.Now()
             eventStatus = customEventHooks.updateEventStatus(eventStatus, callback(eventStatus, unpack(args)))
             if timing then
                 local info = debug and debug.getinfo(callback, "Sl") or {}
                 local source = tostring(info.short_src or "custom") .. ":" .. tostring(info.linedefined or 0)
                 timing.LogSlow(event .. " " .. source, started, args[1])
             end
        end
    end
end

return customEventHooks
