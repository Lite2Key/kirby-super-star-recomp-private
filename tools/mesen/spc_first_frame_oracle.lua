-- Private SPC700 execution/port oracle through the first SNES endFrame.
-- Redirect raw values only beneath .private; commit only the sanitized inventory.
local MAX_EXEC = 100000
local MAX_IO = 20000
local exec_count = 0
local io_count = 0
local event_ordinal = 0
local stopped = false

local function required(state, name)
    local value = state[name]
    if value == nil then error("missing SPC state field: " .. name) end
    return value
end

local function next_event()
    event_ordinal = event_ordinal + 1
    return event_ordinal
end

local function abort(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_SPC_ABORT_V2|%s|%d|%d|%d", reason, exec_count, io_count, event_ordinal))
    emu.stop(1)
end

-- Every phase-sensitive record carries the emulator master clock, both CPU
-- counters, and one capture-wide callback ordinal.  The ordinal is the
-- authoritative tie-break when Mesen reports multiple callbacks at one clock.
local function io(kind, index, value)
    if stopped then return end
    io_count = io_count + 1
    if io_count > MAX_IO then abort("io_limit") return end
    print(string.format("KSS_SPC_IO_V2|%d|%d|%s|%d|%d|%d|%d|%d",
        next_event(), io_count, kind, emu.getMasterClock(),
        emu.getCpuCycleCount(emu.cpuType.spc),
        emu.getCpuCycleCount(emu.cpuType.snes), index, value))
end

emu.addMemoryCallback(function(address, opcode)
    if stopped then return end
    exec_count = exec_count + 1
    if exec_count > MAX_EXEC then abort("exec_limit") return end
    local state = emu.getCpuState(emu.cpuType.spc)
    print(string.format("KSS_SPC_EXEC_V2|%d|%d|%d|%d|%04X|%d|%d|%d|%d|%d|%d",
        next_event(), exec_count, emu.getMasterClock(),
        emu.getCpuCycleCount(emu.cpuType.spc), address & 0xFFFF, opcode,
        required(state,"a"),required(state,"x"),required(state,"y"),
        required(state,"sp"),required(state,"ps")))
end, emu.callbackType.exec, 0x0000, 0xFFFF, emu.cpuType.spc, emu.memType.spcMemory)

emu.addMemoryCallback(function(address, value)
    local offset = address & 0xFFFF
    if offset >= 0x00F4 and offset <= 0x00F7 then
        io("spc_port_write", offset - 0x00F4, value)
    end
end, emu.callbackType.write, 0x00F4, 0x00F7, emu.cpuType.spc, emu.memType.spcMemory)

emu.addMemoryCallback(function(address, value)
    local offset = address & 0xFFFF
    if offset >= 0x00F4 and offset <= 0x00F7 then
        io("spc_port_read", offset - 0x00F4, value)
    end
end, emu.callbackType.read, 0x00F4, 0x00F7, emu.cpuType.spc, emu.memType.spcMemory)

emu.addMemoryCallback(function(address, value)
    local bank = (address >> 16) & 0xFF
    local offset = address & 0xFFFF
    if (bank & 0x40) == 0 and offset >= 0x2140 and offset <= 0x2143 then
        io("scpu_port_write", offset - 0x2140, value)
    end
end, emu.callbackType.write, 0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory)

emu.addEventCallback(function(_cpu)
    if stopped then return end
    stopped = true
    local state = emu.getCpuState(emu.cpuType.spc)
    print(string.format("KSS_SPC_FINAL_V2|%d|%d|%d|%04X|%d|%d|%d|%d|%d",
        next_event(), emu.getMasterClock(), emu.getCpuCycleCount(emu.cpuType.spc),
        required(state,"pc"), required(state,"a"),required(state,"x"),
        required(state,"y"),required(state,"sp"),required(state,"ps")))
    print(string.format("KSS_SPC_END_V2|first_end_frame|%d|%d|%d|%d|%d",
        exec_count,io_count,event_ordinal,emu.getMasterClock(),
        emu.getCpuCycleCount(emu.cpuType.spc)))
    emu.stop(0)
end, emu.eventType.endFrame)

print(string.format("KSS_SPC_START_V2|%d|%d", MAX_EXEC, MAX_IO))
-- Test-runner scripts attach after a handful of power-on SPC instructions.
-- Reset after installing callbacks so the oracle includes the complete IPL path.
emu.reset()
