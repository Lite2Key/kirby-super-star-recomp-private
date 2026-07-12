-- Bounded SA-1-only discovery trace. S-CPU tracing is intentionally omitted
-- so title/menu execution can advance at full speed until the SA-1 is released.
local MAX_EVENTS = 5000
local MAX_FRAMES = 900
local event_count = 0
local frame_count = 0
local stopped = false

local function sa1_cpu_state()
    -- MesenCE 2.2.1's getCpuState(sa1) returns the peripheral wrapper. Its
    -- full-state map contains the actual nested Sa1Cpu serializer fields.
    local state = emu.getState()
    return {
        ps = state["cart.coprocessor.cpu.ps"],
        emulationMode = state["cart.coprocessor.cpu.emulationMode"]
    }
end

local function finish(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_TRACE_END_V1|%d|%s", event_count, reason))
    emu.stop(0)
end

emu.addMemoryCallback(function(address, _value)
    if stopped then return end
    local state = sa1_cpu_state()
    local ps = state.ps
    local emulation = state.emulationMode
    local m8 = emulation or ((ps & 0x20) ~= 0)
    local x8 = emulation or ((ps & 0x10) ~= 0)
    event_count = event_count + 1
    print(string.format(
        "KSS_TRACE_V1|%d|sa1|%d|%06X|%d|%d|%d",
        event_count, emu.getCpuCycleCount(emu.cpuType.sa1), address & 0xFFFFFF,
        emulation and 1 or 0, m8 and 1 or 0, x8 and 1 or 0
    ))
    if event_count >= MAX_EVENTS then finish("limit") end
end, emu.callbackType.exec, 0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory)

emu.addEventCallback(function(_cpu_type)
    frame_count = frame_count + 1
    local phase = frame_count % 90
    emu.setInput({
        Start = phase == 10,
        A = phase == 30 or phase == 50,
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
