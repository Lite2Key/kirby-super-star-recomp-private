-- Private first-endFrame PPU oracle. Redirect output and screenshots only to
-- .private; the sanitizer commits dimensions and pixel digests, not imagery.
local stopped = false

emu.addEventCallback(function(_cpu_type)
    if stopped then return end
    stopped = true
    local size = emu.getScreenSize()
    local buffer = emu.getScreenBuffer()
    local nonblack = 0
    local first = buffer[1]
    local uniform = true
    for i = 1, #buffer do
        if buffer[i] ~= 0 then nonblack = nonblack + 1 end
        if buffer[i] ~= first then uniform = false end
    end
    local state = emu.getState()
    local forced = "missing"
    local brightness = "missing"
    for key, value in pairs(state) do
        local lower = string.lower(key)
        if string.find(lower, "forcedblank", 1, true) then forced = tostring(value) end
        if string.find(lower, "screenbrightness", 1, true) then brightness = tostring(value) end
    end
    emu.takeScreenshot()
    print(string.format("KSS_PPU_FRAME_V1|1|%d|%d|%d|%d|%d|%s|%s|%s",
        size.width, size.height, #buffer, first, nonblack,
        uniform and "true" or "false", forced, brightness))
    emu.stop(0)
end, emu.eventType.endFrame)
