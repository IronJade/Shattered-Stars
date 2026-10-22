// Writes the disassembly of [lo, hi) to OUT (one "address: bytes mnemonic operands" line
// per instruction), disassembling first where nothing is defined; the PSX language names
// the GTE operations.
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript DumpListing.java <out> <lo> <hi>
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Instruction;
import java.io.PrintWriter;

public class DumpListing extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long lo = Long.decode(args[1]), hi = Long.decode(args[2]);
        Address start = toAddr(lo), end = toAddr(hi - 1);
        new DisassembleCommand(new AddressSet(start, end), null, true).applyTo(currentProgram, monitor);
        try (PrintWriter out = new PrintWriter(args[0])) {
            for (long a = lo; a < hi; a += 4) {
                Instruction ins = getInstructionAt(toAddr(a));
                int w = currentProgram.getMemory().getInt(toAddr(a));
                if (ins == null) {
                    out.printf("%08x: %08x  .word%n", a, w);
                } else {
                    out.printf("%08x: %08x  %s%n", a, w, ins.toString());
                }
            }
        }
        println("LISTED " + args[0]);
    }
}
