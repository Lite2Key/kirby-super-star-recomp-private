-- Private, bounded first-frame oracle for MesenCE --testRunner.
-- Raw values must be redirected beneath .private.  The committed sanitizer
-- exports only counts, clocks, and SHA-256 chains.
local MAX_WRITES = 1000000
local writes = 0
local stopped = false

local function required(state, name)
    local value = state[name]
    if value == nil then error("missing CPU state field: " .. name) end
    return value
end

local function emit_state(processor, cpu_type)
    local state = emu.getCpuState(cpu_type)
    local prefix = processor == "sa1" and "cpu." or ""
    local pc = required(state, prefix .. "pc")
        | (required(state, prefix .. "k") << 16)
    print(string.format(
        "KSS_FRAME_STATE_V1|%s|%d|%06X|%d|%d|%d|%d|%d|%d|%d|%d",
        processor, emu.getCpuCycleCount(cpu_type), pc & 0xFFFFFF,
        required(state, prefix .. "a"), required(state, prefix .. "x"),
        required(state, prefix .. "y"), required(state, prefix .. "d"),
        required(state, prefix .. "sp"), required(state, prefix .. "dbr"),
        required(state, prefix .. "ps"),
        required(state, prefix .. "emulationMode") and 1 or 0
    ))
end

local function abort(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_FRAME_ABORT_V1|%s|%d", reason, writes))
    emu.stop(1)
end

local function capture_write(processor, cpu_type)
    return function(address, value)
        if stopped then return end
        writes = writes + 1
        if writes > MAX_WRITES then
            abort("write_limit")
            return
        end
        print(string.format("KSS_FRAME_WRITE_V1|%d|%s|%d|%06X|%d",
            writes, processor, emu.getCpuCycleCount(cpu_type),
            address & 0xFFFFFF, value))
    end
end

emu.addMemoryCallback(capture_write("scpu", emu.cpuType.snes),
    emu.callbackType.write, 0x000000, 0xFFFFFF,
    emu.cpuType.snes, emu.memType.snesMemory)
emu.addMemoryCallback(capture_write("sa1", emu.cpuType.sa1),
    emu.callbackType.write, 0x000000, 0xFFFFFF,
    emu.cpuType.sa1, emu.memType.sa1Memory)

emu.addEventCallback(function(_cpu)
    if stopped then return end
    stopped = true
    emit_state("scpu", emu.cpuType.snes)
    emit_state("sa1", emu.cpuType.sa1)
    print(string.format("KSS_FRAME_END_V1|1|%d|%d|%d|%d",
        emu.getMasterClock(), emu.getCpuCycleCount(emu.cpuType.snes),
        emu.getCpuCycleCount(emu.cpuType.sa1), writes))
    emu.stop(0)
end, emu.eventType.endFrame)

print(string.format("KSS_FRAME_START_V1|%d", MAX_WRITES))
