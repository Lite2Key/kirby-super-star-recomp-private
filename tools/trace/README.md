# Trace sanitizer

The importer recognizes only `KSS_TRACE_V1` records and reduces them to
address/mode coverage and per-CPU control-flow transitions. Emulator diagnostics
are ignored. The sanitized JSON contains no opcode values, memory contents,
register values, ROM data, screenshots, or source filesystem paths.
