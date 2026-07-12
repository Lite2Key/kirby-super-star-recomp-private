// Exports address-only program metadata suitable for ROM-free analysis artifacts.
// @category KSS Recomp

import java.io.BufferedWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;

public class ExportSanitizedProgram extends GhidraScript {
    private static String json(String value) {
        if (value == null) {
            return "null";
        }
        StringBuilder result = new StringBuilder(value.length() + 16);
        result.append('"');
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '"': result.append("\\\""); break;
                case '\\': result.append("\\\\"); break;
                case '\b': result.append("\\b"); break;
                case '\f': result.append("\\f"); break;
                case '\n': result.append("\\n"); break;
                case '\r': result.append("\\r"); break;
                case '\t': result.append("\\t"); break;
                default:
                    if (c < 0x20) {
                        result.append(String.format("\\u%04x", (int)c));
                    }
                    else {
                        result.append(c);
                    }
            }
        }
        return result.append('"').toString();
    }

    private static String address(Address value) {
        return "{\"space\":" + json(value.getAddressSpace().getName()) +
            ",\"offset\":\"0x" + Long.toUnsignedString(value.getOffset(), 16).toUpperCase() + "\"}";
    }

    private static void writeItems(BufferedWriter writer, List<String> items, String indent)
            throws IOException {
        for (int i = 0; i < items.size(); i++) {
            writer.write(indent);
            writer.write(items.get(i));
            if (i + 1 < items.size()) {
                writer.write(',');
            }
            writer.newLine();
        }
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1 || arguments[0].isBlank()) {
            throw new IllegalArgumentException("usage: ExportSanitizedProgram.java <output.json>");
        }
        if (currentProgram == null) {
            throw new IllegalStateException("the exporter requires an open program");
        }

        Path output = Paths.get(arguments[0]).toAbsolutePath().normalize();
        Path parent = output.getParent();
        if (parent == null) {
            throw new IllegalArgumentException("output must have a parent directory");
        }
        Files.createDirectories(parent);

        List<String> blocks = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            blocks.add("{\"start\":" + address(block.getStart()) +
                ",\"end\":" + address(block.getEnd()) +
                ",\"permissions\":{\"read\":" + block.isRead() +
                ",\"write\":" + block.isWrite() +
                ",\"execute\":" + block.isExecute() + "}}");
        }
        blocks.sort(Comparator.naturalOrder());

        List<String> entryPoints = new ArrayList<>();
        AddressIterator entries = currentProgram.getSymbolTable().getExternalEntryPointIterator();
        while (entries.hasNext()) {
            entryPoints.add(address(entries.next()));
        }
        entryPoints.sort(Comparator.naturalOrder());

        List<String> functions = new ArrayList<>();
        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            functions.add("{\"entry\":" + address(function.getEntryPoint()) +
                ",\"name\":" + json(function.getName(false)) + "}");
        }
        functions.sort(Comparator.naturalOrder());

        List<String> symbols = new ArrayList<>();
        for (Symbol symbol : currentProgram.getSymbolTable().getAllSymbols(true)) {
            if (symbol.getAddress() == null || !symbol.getAddress().isMemoryAddress()) {
                continue;
            }
            symbols.add("{\"address\":" + address(symbol.getAddress()) +
                ",\"type\":" + json(symbol.getSymbolType().toString()) + "}");
        }
        symbols.sort(Comparator.naturalOrder());

        Path temporary = Files.createTempFile(parent, output.getFileName().toString(), ".tmp");
        try (BufferedWriter writer = Files.newBufferedWriter(temporary, StandardCharsets.UTF_8)) {
            writer.write("{\n");
            writer.write("  \"schema_version\": 1,\n");
            writer.write("  \"program_id\": " + json(currentProgram.getDomainFile().getFileID()) + ",\n");
            writer.write("  \"language_id\": " + json(currentProgram.getLanguageID().getIdAsString()) + ",\n");
            writer.write("  \"compiler_spec_id\": " +
                json(currentProgram.getCompilerSpec().getCompilerSpecID().getIdAsString()) + ",\n");
            writer.write("  \"image_base\": " + address(currentProgram.getImageBase()) + ",\n");
            writer.write("  \"memory_blocks\": [\n");
            writeItems(writer, blocks, "    ");
            writer.write("  ],\n  \"entry_points\": [\n");
            writeItems(writer, entryPoints, "    ");
            writer.write("  ],\n  \"functions\": [\n");
            writeItems(writer, functions, "    ");
            writer.write("  ],\n  \"symbols\": [\n");
            writeItems(writer, symbols, "    ");
            writer.write("  ]\n}\n");
        }
        try {
            Files.move(temporary, output, StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING);
        }
        catch (java.nio.file.AtomicMoveNotSupportedException exception) {
            Files.move(temporary, output, StandardCopyOption.REPLACE_EXISTING);
        }
        println("Sanitized metadata exported to " + output);
    }
}
