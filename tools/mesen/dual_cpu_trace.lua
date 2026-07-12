-- Bounded, ROM-byte-free S-CPU/SA-1 execution trace for MesenCE --testRunner.
-- Raw output is sensitive reference evidence and must be redirected only to
-- .private/traces by scripts/run-mesen-trace.ps1.

local EVENTS_PER_CPU = 5000
local MAX_EVENTS = EVENTS_PER_CPU * 2
local MAX_FRAMES = 300
local event_count = 0
local frame_count = 0
local processor_events = { scpu = 0, sa1 = 0 }
local callback_refs = {}
local callback_removed = { scpu = false, sa1 = false }
local stopped = false

local function state_field(state, name)
    local value = state[name]
    if value == nil then
        value = state[string.lower(name)]
    end
    if value == nil then
        value = state[string.lower(string.sub(name, 1, 1)) .. string.sub(name, 2)]
    end
    if value == nil then
        value = state["_state." .. name]
    end
    if value == nil then
        error("MesenCE CPU state is missing required field: " .. name)
    end
    return value
end

local function bit(value)
    return value and 1 or 0
end

local function cpu_mode_state(processor, cpu_type)
    if processor == "sa1" then
        -- MesenCE 2.2.1's getCpuState(sa1) serializes the SA-1 wrapper
        -- instead of Sa1Cpu. The full-state map exposes the nested CPU state.
        local state = emu.getState()
        return {
            ps = state["cart.coprocessor.cpu.ps"],
            emulationMode = state["cart.coprocessor.cpu.emulationMode"]
        }
    end
    return emu.getCpuState(cpu_type)
end

local function capture(processor, cpu_type)
    return function(address, _value)
        if stopped then
            return
        end
        if processor_events[processor] >= EVENTS_PER_CPU then
            return
        end

        local state = cpu_mode_state(processor, cpu_type)
        local ps = state_field(state, "PS")
        local emulation = state_field(state, "EmulationMode")
        local m8 = emulation or ((ps & 0x20) ~= 0)
        local x8 = emulation or ((ps & 0x10) ~= 0)
        local cycle = emu.getCpuCycleCount(cpu_type)

        event_count = event_count + 1
        processor_events[processor] = processor_events[processor] + 1
        -- Deliberately excludes callback value/opcode, registers, and memory.
        print(string.format(
            "KSS_TRACE_V1|%d|%s|%d|%06X|%d|%d|%d",
            event_count, processor, cycle, address & 0xFFFFFF,
            bit(emulation), bit(m8), bit(x8)
        ))

        if processor_events.scpu >= EVENTS_PER_CPU and processor_events.sa1 >= EVENTS_PER_CPU then
            stopped = true
            print(string.format("KSS_TRACE_END_V1|%d|limit", event_count))
            emu.stop(0)
        end
    end
end

local function finish(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_TRACE_END_V1|%d|%s", event_count, reason))
    emu.stop(0)
end

callback_refs.scpu = emu.addMemoryCallback(
    capture("scpu", emu.cpuType.snes), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory
)
callback_refs.sa1 = emu.addMemoryCallback(
    capture("sa1", emu.cpuType.sa1), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory
)

-- Advance deterministically beyond the attract/title shell so the SA-1 is
-- released. Pulses are one polled frame wide and all unspecified gameplay
-- buttons remain false during this bounded discovery scenario.
emu.addEventCallback(function(_cpu_type)
    frame_count = frame_count + 1
    if frame_count == 1 then print("KSS_TRACE_FRAME_V1|1") end
    if processor_events.scpu >= EVENTS_PER_CPU and not callback_removed.scpu then
        emu.removeMemoryCallback(
            callback_refs.scpu, emu.callbackType.exec,
            0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory
        )
        callback_removed.scpu = true
        print("KSS_TRACE_CALLBACK_REMOVED_V1|scpu")
    end
    if processor_events.sa1 >= EVENTS_PER_CPU and not callback_removed.sa1 then
        emu.removeMemoryCallback(
            callback_refs.sa1, emu.callbackType.exec,
            0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory
        )
        callback_removed.sa1 = true
        print("KSS_TRACE_CALLBACK_REMOVED_V1|sa1")
    end
    local phase = frame_count % 120
    emu.setInput({
        Start = phase == 30,
        A = phase == 60 or phase == 90,
        B = false,
        Select = false,
        Up = false,
        Down = false,
        Left = false,
        Right = false
    }, 0)
    if frame_count >= MAX_FRAMES then finish("complete") end
end, emu.eventType.inputPolled)

print(string.format("KSS_TRACE_START_V1|%d", MAX_EVENTS))
