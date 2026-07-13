"""Extract a value-safe SPC IPL upload handshake contract from a private V1 log.

The raw Mesen log contains game payload bytes and must remain ignored.  This
module emits only fixed IPL tokens, control-flow identities, counters, and
aggregate transfer progress suitable for a tracked technical artifact.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class SpcProtocolError(ValueError):
    """The capture does not prove the expected IPL upload handshake."""


@dataclass(frozen=True)
class Event:
    kind: str
    counter: int
    pc: int | None = None
    opcode: int | None = None
    direction: str | None = None
    port: int | None = None
    value: int | None = None
    ps: int | None = None


ACK_PATH = (
    (0xFFD2, 0xD0, "BNE start-wait (taken using the preceding failed compare)"),
    (0xFFCF, 0x78, "CMP input F4 with start token CC"),
    (0xFFD2, 0xD0, "BNE start-wait (not taken)"),
    (0xFFD4, 0x2F, "BRA upload setup"),
    (0xFFEF, 0xBA, "MOVW YA,F6 (destination)"),
    (0xFFF1, 0xDA, "MOVW dp,YA (install destination pointer)"),
    (0xFFF3, 0xBA, "MOVW YA,F4 (start token and transfer flag)"),
    (0xFFF5, 0xC4, "MOV F4,A (publish start acknowledgement)"),
)


def _parse(path: Path) -> list[Event]:
    events: list[Event] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split("|")
        if parts[0] == "KSS_SPC_EXEC_V1":
            if len(parts) != 10:
                raise SpcProtocolError("malformed SPC execution record")
            events.append(Event(
                "exec", int(parts[2]), pc=int(parts[3], 16), opcode=int(parts[4]),
                ps=int(parts[9]),
            ))
        elif parts[0] == "KSS_SPC_PORT_V1":
            if len(parts) != 6:
                raise SpcProtocolError("malformed SPC port record")
            events.append(Event(
                "port", int(parts[3]), direction=parts[2],
                port=int(parts[4]), value=int(parts[5]),
            ))
    if not events:
        raise SpcProtocolError("capture contains no SPC events")
    return events


def _find(
    events: list[Event], *, start: int = 0, direction: str, port: int, value: int
) -> int:
    for index in range(start, len(events)):
        event = events[index]
        if (
            event.kind == "port" and event.direction == direction
            and event.port == port and event.value == value
        ):
            return index
    raise SpcProtocolError(
        f"missing {direction} port {port} value {value:02X} event"
    )


def summarize_ipl_upload_protocol(path: Path) -> dict[str, Any]:
    events = _parse(path)
    ready_aa = _find(events, direction="spc_to_cpu", port=0, value=0xAA)
    ready_bb = _find(events, start=ready_aa + 1, direction="spc_to_cpu", port=1, value=0xBB)
    start_cc = _find(events, start=ready_bb + 1, direction="cpu_to_spc", port=0, value=0xCC)
    ack_cc = _find(events, start=start_cc + 1, direction="spc_to_cpu", port=0, value=0xCC)

    setup: dict[int, int] = {}
    setup_order: list[int] = []
    for event in events[ready_bb + 1:start_cc]:
        if event.kind == "port" and event.direction == "cpu_to_spc":
            assert event.port is not None and event.value is not None
            setup[event.port] = event.value
            setup_order.append(event.port)
    if setup.get(2) != 0x00 or setup.get(3) != 0x07 or setup.get(1) != 0x01:
        raise SpcProtocolError("unexpected KSS destination or transfer flag setup")
    if setup_order != [2, 3, 1]:
        raise SpcProtocolError("unexpected KSS upload setup write order")

    path_exec = [event for event in events[start_cc + 1:ack_cc] if event.kind == "exec"]
    identities = tuple((event.pc, event.opcode) for event in path_exec)
    expected = tuple((pc, opcode) for pc, opcode, _ in ACK_PATH)
    if identities != expected:
        raise SpcProtocolError("start acknowledgement instruction path does not match")
    if path_exec[0].ps is None or (path_exec[0].ps & 0x02) != 0:
        raise SpcProtocolError("first post-write BNE did not retain a failed compare")
    if path_exec[2].ps is None or (path_exec[2].ps & 0x02) == 0:
        raise SpcProtocolError("second BNE did not observe the CC compare match")
    if path_exec[-1].counter > events[ack_cc].counter:
        raise SpcProtocolError("acknowledgement precedes its SPC write instruction")

    sent = [
        event.value for event in events[ack_cc + 1:]
        if event.kind == "port" and event.direction == "cpu_to_spc" and event.port == 0
    ]
    echoed = [
        event.value for event in events[ack_cc + 1:]
        if event.kind == "port" and event.direction == "spc_to_cpu" and event.port == 0
    ]
    data_count = sum(
        event.kind == "port" and event.direction == "cpu_to_spc" and event.port == 1
        for event in events[ack_cc + 1:]
    )
    if sent != [index & 0xFF for index in range(len(sent))]:
        raise SpcProtocolError("S-CPU transfer counters are not contiguous from zero")
    if echoed != sent[:len(echoed)]:
        raise SpcProtocolError("SPC echoed counters are not the acknowledged sent prefix")
    if data_count != len(sent):
        raise SpcProtocolError("counter and payload-byte write counts disagree")
    if len(sent) - len(echoed) not in (0, 1):
        raise SpcProtocolError("capture has more than one unacknowledged transfer byte")

    ready_events = (events[ready_aa], events[ready_bb])
    first_post_write = path_exec[0]
    ack_event = events[ack_cc]
    return {
        "schema_version": 1,
        "boundary": "first_snes_end_frame",
        "protocol": "snes_spc_ipl_upload",
        "fixed_tokens": {"ready_port0": 0xAA, "ready_port1": 0xBB, "start": 0xCC},
        "ready": {
            "spc_counters": [event.counter for event in ready_events],
            "instruction_pcs": [0xFFC9, 0xFFCC],
        },
        "kss_upload_setup": {
            "destination": 0x0700,
            "transfer_flag": 1,
            "write_order": [2, 3, 1, 0],
        },
        "start_acknowledgement": {
            "ordering": "write lands after a failed CMP read and before its BNE",
            "first_post_write_spc_counter": first_post_write.counter,
            "ack_spc_counter": ack_event.counter,
            "mesen_counter_delta_from_first_post_write_boundary": (
                ack_event.counter - first_post_write.counter
            ),
            "architectural_spc_cycles_from_first_post_write_boundary": (
                ack_event.counter - first_post_write.counter
            ) // 2,
            "instruction_path": [
                {"pc": pc, "opcode": opcode, "effect": effect}
                for pc, opcode, effect in ACK_PATH
            ],
        },
        "transfer_at_boundary": {
            "bytes_sent": len(sent),
            "counters_echoed": len(echoed),
            "pending_counter": sent[-1] if len(sent) > len(echoed) else None,
            "payload_values_included": False,
        },
        "scheduler_acceptance": [
            "CPU-to-SPC F4 commits must be ordered against the SPC FFCF read phase",
            "SPC FFF5 writes publish to the opposite latch before the next FFF7 boundary",
            "S-CPU reads must observe only committed SPC-to-CPU latch values",
            "A transfer counter is complete only after the SPC echoes it on output F4",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(summarize_ipl_upload_protocol(args.input), indent=2) + "\n"
    if args.output is None:
        print(rendered, end="")
        return 0
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"stale or missing SPC protocol artifact: {args.output}")
        return 0
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
