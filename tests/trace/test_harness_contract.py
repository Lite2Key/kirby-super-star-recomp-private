from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_lua_uses_documented_dual_cpu_callbacks_and_bounded_capture() -> None:
    script = (ROOT / "tools" / "mesen" / "dual_cpu_trace.lua").read_text(encoding="utf-8")
    required = (
        "emu.addMemoryCallback", "emu.callbackType.exec",
        "emu.cpuType.snes", "emu.memType.snesMemory",
        "emu.cpuType.sa1", "emu.memType.sa1Memory",
        "emu.getCpuState", "emu.getState", "emu.getCpuCycleCount", "print", "emu.stop(0)",
        "local EVENTS_PER_CPU = 5000",
    )
    for item in required:
        assert item in script
    assert "_value" in script
    assert "string.format" in script
    assert 'state["cart.coprocessor.cpu.ps"]' in script
    assert 'state["cart.coprocessor.cpu.emulationMode"]' in script


def test_sa1_only_harness_uses_nested_cpu_state_workaround() -> None:
    script = (ROOT / "tools" / "mesen" / "sa1_trace.lua").read_text(encoding="utf-8")
    assert "emu.cpuType.sa1" in script
    assert "emu.getState()" in script
    assert 'state["cart.coprocessor.cpu.ps"]' in script
    assert 'state["cart.coprocessor.cpu.emulationMode"]' in script
    assert "local MAX_EVENTS = 5000" in script
    assert "emu.stop(0)" in script


def test_runner_confines_raw_output_to_private_trace_folder() -> None:
    script = (ROOT / "scripts" / "run-mesen-trace.ps1").read_text(encoding="utf-8")
    assert '".private\\traces"' in script
    assert "tools\\trace\\kss_trace\\run_mesen.py" in script
    assert "--raw-log $rawLog" in script
    assert "OutputName must be a plain .log filename" in script
