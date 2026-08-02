-- Private first-visible-frame oracle. Screenshots and raw pixel data stay in
-- .private; only sanitized dimensions, counters, and digests may be committed.
local MAX_FRAMES = 600
local ARGB_CHUNK_PIXELS = 512
local frame = 0
local stopped = false

local function emit_framebuffer(buffer)
    local chunks = math.ceil(#buffer / ARGB_CHUNK_PIXELS)
    print(string.format("KSS_PPU_VISIBLE_ARGB_START_V1|%d|%d", #buffer, chunks))
    for index = 1, chunks do
        local first = ((index - 1) * ARGB_CHUNK_PIXELS) + 1
        local last = math.min(first + ARGB_CHUNK_PIXELS - 1, #buffer)
        local encoded = {}
        for pixel = first, last do
            encoded[#encoded + 1] = string.format("%08X", buffer[pixel] & 0xFFFFFFFF)
        end
        print(string.format("KSS_PPU_VISIBLE_ARGB_CHUNK_V1|%d|%s", index, table.concat(encoded)))
    end
    print(string.format("KSS_PPU_VISIBLE_ARGB_END_V1|%d|%d", chunks, #buffer))
end

local function finish(reason, nonblack, size, buffer)
    if stopped then return end
    stopped = true
    if reason == "visible" then emit_framebuffer(buffer) end
    print(string.format(
        "KSS_PPU_VISIBLE_V1|%s|%d|%d|%d|%d|%d|%d|%d",
        reason, frame, size.width, size.height, #buffer, nonblack,
        emu.getMasterClock(), emu.getCpuCycleCount(emu.cpuType.snes)))
    emu.stop(reason == "visible" and 0 or 1)
end

emu.addEventCallback(function(_cpu_type)
    if stopped then return end
    frame = frame + 1
    local size = emu.getScreenSize()
    local buffer = emu.getScreenBuffer()
    local nonblack = 0
    for index = 1, #buffer do
        if buffer[index] ~= 0 then nonblack = nonblack + 1 end
    end
    if nonblack > 0 then
        finish("visible", nonblack, size, buffer)
    elseif frame >= MAX_FRAMES then
        finish("frame_limit", nonblack, size, buffer)
    elseif frame == 1 or frame % 60 == 0 then
        print(string.format("KSS_PPU_VISIBLE_WAIT_V1|%d|%d|%d",
            frame, emu.getMasterClock(), emu.getCpuCycleCount(emu.cpuType.snes)))
    end
end, emu.eventType.endFrame)

print(string.format("KSS_PPU_VISIBLE_START_V1|%d", MAX_FRAMES))
-- Install the callback before reset so frame numbering starts at power-on.
emu.reset()
