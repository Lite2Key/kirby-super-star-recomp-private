-- Compact, identity-only route capture from reset through the first visible
-- endFrame. Instruction identities and per-CPU edges are counted in MesenCE;
-- no opcode, register, memory, or framebuffer values are printed.
-- The raw console log must remain under .private until it is sanitized by
-- kss_trace.compact_route_importer.
local MAX_EVENTS = 5000000
local MAX_FRAMES = 600
local MAX_BLOCKS = 250000
local MAX_EDGES = 500000

local event_count = 0
local frame_count = 0
local block_count = 0
local edge_count = 0
local stopped = false
local blocks = {}
local edges = {}
local previous = {}
local modes = {}

-- These are the only 65C816 instructions whose completed execution can alter
-- the E/M/X decode state. Callback values are used only as ISA selectors
-- inside MesenCE and are never emitted.
local MODE_MUTATORS = {
    [0x28] = true, -- PLP
    [0x40] = true, -- RTI
    [0xC2] = true, -- REP
    [0xE2] = true, -- SEP
    [0xFB] = true  -- XCE
}

local function state_field(state, name)
    local value = state[name]
    if value == nil then value = state[string.lower(name)] end
    if value == nil then value = state[string.lower(string.sub(name, 1, 1)) .. string.sub(name, 2)] end
    if value == nil then value = state["_state." .. name] end
    if value == nil then error("MesenCE CPU state is missing required field: " .. name) end
    return value
end

local function bit(value) return value and 1 or 0 end

local function read_cpu_mode(processor, cpu_type)
    if processor == "sa1" then
        -- MesenCE 2.2.1 serializes the SA-1 peripheral wrapper from
        -- getCpuState(sa1). The nested CPU flags are available only here.
        local state = emu.getState()
        return {
            ps = state["cart.coprocessor.cpu.ps"],
            emulationMode = state["cart.coprocessor.cpu.emulationMode"]
        }
    end
    return emu.getCpuState(cpu_type)
end

local function current_mode(processor, cpu_type)
    local cached = modes[processor]
    if cached ~= nil and not cached.refresh then return cached end
    local state = read_cpu_mode(processor, cpu_type)
    local ps = state_field(state, "PS")
    local emulation = state_field(state, "EmulationMode")
    cached = {
        emulation = bit(emulation),
        m8 = bit(emulation or ((ps & 0x20) ~= 0)),
        x8 = bit(emulation or ((ps & 0x10) ~= 0)),
        refresh = false
    }
    modes[processor] = cached
    return cached
end

local function identity(processor, cpu_type, address)
    local mode = current_mode(processor, cpu_type)
    local item = {
        processor = processor,
        pc = address & 0xFFFFFF,
        emulation = mode.emulation,
        m8 = mode.m8,
        x8 = mode.x8
    }
    item.key = string.format("%s|%06X|%d|%d|%d",
        item.processor, item.pc, item.emulation, item.m8, item.x8)
    return item
end

local function abort(reason)
    if stopped then return end
    stopped = true
    print(string.format("KSS_COMPACT_ROUTE_ABORT_V1|%s|%d|%d|%d|%d",
        reason, event_count, frame_count, block_count, edge_count))
    emu.stop(1)
end

local function capture(processor, cpu_type)
    return function(address, value)
        if stopped then return end
        if event_count >= MAX_EVENTS then abort("event_limit"); return end

        local current = identity(processor, cpu_type, address)
        local cycle = emu.getCpuCycleCount(cpu_type)
        event_count = event_count + 1

        local block = blocks[current.key]
        if block == nil then
            if block_count >= MAX_BLOCKS then abort("block_limit"); return end
            block = {
                identity = current,
                hits = 0,
                first_cycle = cycle,
                last_cycle = cycle
            }
            blocks[current.key] = block
            block_count = block_count + 1
        end
        block.hits = block.hits + 1
        block.last_cycle = cycle

        local prior = previous[processor]
        if prior ~= nil then
            local edge_key = prior.key .. ">" .. current.key
            local edge = edges[edge_key]
            if edge == nil then
                if edge_count >= MAX_EDGES then abort("edge_limit"); return end
                edge = { source = prior, target = current, hits = 0 }
                edges[edge_key] = edge
                edge_count = edge_count + 1
            end
            edge.hits = edge.hits + 1
        end
        previous[processor] = current

        if MODE_MUTATORS[value & 0xFF] then modes[processor].refresh = true end

        if event_count >= MAX_EVENTS then abort("event_limit") end
    end
end

local function sorted_keys(items)
    local keys = {}
    for key, _ in pairs(items) do keys[#keys + 1] = key end
    table.sort(keys)
    return keys
end

local function emit_identity(item)
    return string.format("%s|%06X|%d|%d|%d",
        item.processor, item.pc, item.emulation, item.m8, item.x8)
end

local function finish(size, pixel_count, nonblack)
    if stopped then return end
    stopped = true

    for _, key in ipairs(sorted_keys(blocks)) do
        local block = blocks[key]
        print(string.format("KSS_COMPACT_ROUTE_BLOCK_V1|%s|%d|%d|%d",
            emit_identity(block.identity), block.hits,
            block.first_cycle, block.last_cycle))
    end
    for _, key in ipairs(sorted_keys(edges)) do
        local edge = edges[key]
        print(string.format("KSS_COMPACT_ROUTE_EDGE_V1|%s|%s|%d",
            emit_identity(edge.source), emit_identity(edge.target), edge.hits))
    end
    print(string.format(
        "KSS_COMPACT_ROUTE_END_V1|%d|visible|%d|%d|%d|%d|%d|%d|%d",
        event_count, frame_count, size.width, size.height, pixel_count,
        nonblack, block_count, edge_count))
    emu.stop(0)
end

emu.addMemoryCallback(capture("scpu", emu.cpuType.snes), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.snes, emu.memType.snesMemory)
emu.addMemoryCallback(capture("sa1", emu.cpuType.sa1), emu.callbackType.exec,
    0x000000, 0xFFFFFF, emu.cpuType.sa1, emu.memType.sa1Memory)

emu.addEventCallback(function(_cpu_type)
    if stopped then return end
    frame_count = frame_count + 1
    local size = emu.getScreenSize()
    local buffer = emu.getScreenBuffer()
    local nonblack = 0
    for index = 1, #buffer do
        if buffer[index] ~= 0 then nonblack = nonblack + 1 end
    end
    if nonblack > 0 then
        finish(size, #buffer, nonblack)
    elseif frame_count >= MAX_FRAMES then
        abort("frame_limit")
    end
end, emu.eventType.endFrame)

print(string.format("KSS_COMPACT_ROUTE_START_V1|%d|%d|%d|%d",
    MAX_EVENTS, MAX_FRAMES, MAX_BLOCKS, MAX_EDGES))
-- Install all callbacks before reset so the route starts at power-on.
emu.reset()
