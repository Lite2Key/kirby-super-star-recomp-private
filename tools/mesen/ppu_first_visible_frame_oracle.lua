-- Private first-visible-frame oracle. Screenshots and raw pixel data stay in
-- .private; only sanitized dimensions, counters, and digests may be committed.
local MAX_FRAMES = 600
local frame = 0
local stopped = false

local function finish(reason, nonblack, size, buffer)
    if stopped then return end
    stopped = true
    print(string.format(
        "KSS_PPU_VISIBLE_V1|%s|%d|%d|%d|%d|%d|%d|%d",
        reason, frame, size.width, size.height, #buffer, nonblack,
        emu.getMasterClock(), emu.getCpuCycleCount(emu.cpuType.snes)))
    if reason == "visible" then emu.takeScreenshot() end
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
