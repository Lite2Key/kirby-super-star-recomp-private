-- Private register/cycle/write oracle for differential validation.
-- Redirect output only to ignored .private/ paths. Nothing from this log is
-- suitable for Git; tools/differential emits a value-free summary.
local EVENTS_PER_CPU = 256
local count = { scpu = 0, sa1 = 0 }
local total = 0
local stopped = false

local function field(state, name)
    local value = state[name]
    if value == nil then error("missing CPU state field: " .. name) end
    return value
end

local function mode_state(processor, cpu_type)
    if processor == "sa1" then
        local all = emu.getState()
        local prefix = "cart.coprocessor.cpu."
        return {
            a = all[prefix .. "a"], x = all[prefix .. "x"],
            y = all[prefix .. "y"], d = all[prefix .. "d"],
            sp = all[prefix .. "sp"], dbr = all[prefix .. "dbr"],
            ps = all[prefix .. "ps"], emulationMode = all[prefix .. "emulationMode"]
        }
    end
    return emu.getCpuState(cpu_type)
end

local function finish(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_DIFF_END_V1|%d|%s|%d|%d", total, reason, count.scpu, count.sa1))
    emu.stop(0)
end

local function capture(processor, cpu_type)
    return function(address, _value)
        if stopped or count[processor] >= EVENTS_PER_CPU then return end
        local state = mode_state(processor, cpu_type)
        count[processor] = count[processor] + 1
        total = total + 1
        print(string.format(
            "KSS_DIFF_STATE_V1|%d|%s|%d|%06X|%d|%d|%d|%d|%d|%d|%d|%d",
            total, processor, emu.getCpuCycleCount(cpu_type), address & 0xFFFFFF,
            field(state, "a"), field(state, "x"), field(state, "y"),
            field(state, "d"), field(state, "sp"), field(state, "dbr"),
            field(state, "ps"), field(state, "emulationMode") and 1 or 0
        ))
        if count.scpu >= EVENTS_PER_CPU and count.sa1 >= EVENTS_PER_CPU then finish("limit") end
    end
end

local function capture_write(processor)
    return function(address, value)
        if stopped then return end
        print(string.format("KSS_DIFF_WRITE_V1|%s|%06X|%d", processor, address & 0xFFFFFF, value))
    end
end

emu.addMemoryCallback(capture("scpu", emu.cpuType.snes), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory)
emu.addMemoryCallback(capture("sa1", emu.cpuType.sa1), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory)
emu.addMemoryCallback(capture_write("scpu"), emu.callbackType.write,
    0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory)
emu.addMemoryCallback(capture_write("sa1"), emu.callbackType.write,
    0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory)

print(string.format("KSS_DIFF_START_V1|%d", EVENTS_PER_CPU))
