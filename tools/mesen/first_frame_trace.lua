-- Bounded ROM-byte-free discovery trace through the first rendered SNES frame.
-- Raw output is private; only tools/trace/kss_trace/importer.py output is safe.
local MAX_EVENTS = 250000
local event_count = 0
local processor_events = { scpu = 0, sa1 = 0 }
local stopped = false

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

local function finish(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_TRACE_END_V1|%d|%s", event_count, reason))
    emu.stop(0)
end

local function capture(processor, cpu_type)
    return function(address, _value)
        if stopped then return end
        if event_count >= MAX_EVENTS then finish("limit"); return end
        local state = cpu_mode_state(processor, cpu_type)
        local ps = state_field(state, "PS")
        local emulation = state_field(state, "EmulationMode")
        local m8 = emulation or ((ps & 0x20) ~= 0)
        local x8 = emulation or ((ps & 0x10) ~= 0)
        event_count = event_count + 1
        processor_events[processor] = processor_events[processor] + 1
        print(string.format("KSS_TRACE_V1|%d|%s|%d|%06X|%d|%d|%d",
            event_count, processor, emu.getCpuCycleCount(cpu_type), address & 0xFFFFFF,
            bit(emulation), bit(m8), bit(x8)))
        if event_count >= MAX_EVENTS then finish("limit") end
    end
end

emu.addMemoryCallback(capture("scpu", emu.cpuType.snes), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory)
emu.addMemoryCallback(capture("sa1", emu.cpuType.sa1), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory)

emu.addEventCallback(function(_cpu_type)
    print(string.format("KSS_TRACE_FIRST_FRAME_V1|%d|%d|%d",
        event_count, processor_events.scpu, processor_events.sa1))
    finish("complete")
end, emu.eventType.endFrame)

print(string.format("KSS_TRACE_START_V1|%d", MAX_EVENTS))
