-- Identity-only continuation trace from the first endFrame boundary to the
-- first later nonblack frame. Raw output stays private until sanitized by
-- kss_trace.post_frame_importer.
local MAX_EVENTS = 1000000
local MAX_FRAMES = 600
local event_count = 0
local frames_elapsed = 0
local armed = false
local stopped = false
local last = {}

local function state_field(state, name)
    local value = state[name]
    if value == nil then value = state[string.lower(name)] end
    if value == nil then value = state[string.lower(string.sub(name, 1, 1)) .. string.sub(name, 2)] end
    if value == nil then value = state["_state." .. name] end
    if value == nil then error("MesenCE CPU state is missing required field: " .. name) end
    return value
end

local function bit(value) return value and 1 or 0 end

local function cpu_mode_state(processor, cpu_type)
    if processor == "sa1" then
        local state = emu.getState()
        return {
            ps = state["cart.coprocessor.cpu.ps"],
            emulationMode = state["cart.coprocessor.cpu.emulationMode"]
        }
    end
    return emu.getCpuState(cpu_type)
end

local function observation(processor, cpu_type, address)
    local state = cpu_mode_state(processor, cpu_type)
    local ps = state_field(state, "PS")
    local emulation = state_field(state, "EmulationMode")
    return {
        processor = processor,
        cycle = emu.getCpuCycleCount(cpu_type),
        pc = address & 0xFFFFFF,
        emulation = bit(emulation),
        m8 = bit(emulation or ((ps & 0x20) ~= 0)),
        x8 = bit(emulation or ((ps & 0x10) ~= 0))
    }
end

local function abort(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_POST_FRAME_ABORT_V1|%s|%d|%d", reason, event_count, frames_elapsed))
    emu.stop(1)
end

local function capture(processor, cpu_type)
    return function(address, _value)
        if stopped then return end
        local current = observation(processor, cpu_type, address)
        last[processor] = current
        if not armed then return end
        if event_count >= MAX_EVENTS then abort("event_limit"); return end
        event_count = event_count + 1
        print(string.format("KSS_TRACE_V1|%d|%s|%d|%06X|%d|%d|%d",
            event_count, processor, current.cycle, current.pc,
            current.emulation, current.m8, current.x8))
        if event_count >= MAX_EVENTS then abort("event_limit") end
    end
end

emu.addMemoryCallback(capture("scpu", emu.cpuType.snes), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory)
emu.addMemoryCallback(capture("sa1", emu.cpuType.sa1), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory)

emu.addEventCallback(function(_cpu_type)
    if stopped then return end
    if not armed then
        if last.scpu == nil or last.sa1 == nil then abort("missing_anchor"); return end
        armed = true
        print(string.format("KSS_POST_FRAME_START_V1|%d|%d", MAX_EVENTS, MAX_FRAMES))
        for _, processor in ipairs({"scpu", "sa1"}) do
            local item = last[processor]
            print(string.format("KSS_POST_FRAME_ANCHOR_V1|%s|%d|%06X|%d|%d|%d",
                processor, item.cycle, item.pc, item.emulation, item.m8, item.x8))
        end
        return
    end

    frames_elapsed = frames_elapsed + 1
    local size = emu.getScreenSize()
    local buffer = emu.getScreenBuffer()
    local nonblack = 0
    for index = 1, #buffer do
        if buffer[index] ~= 0 then nonblack = nonblack + 1 end
    end
    if nonblack > 0 then
        stopped = true
        print(string.format("KSS_POST_FRAME_END_V1|%d|visible|%d|%d|%d|%d|%d",
            event_count, frames_elapsed, size.width, size.height, #buffer, nonblack))
        emu.stop(0)
    elseif frames_elapsed >= MAX_FRAMES then
        abort("frame_limit")
    end
end, emu.eventType.endFrame)

-- The first marker is intentionally emitted at the first endFrame, after both
-- CPU anchors have been observed. Install callbacks before power-on reset.
emu.reset()
