// Rebuilds the functions of a code range [lo, hi) from its real entry points: the targets
// of jal instructions in the source ranges (default: the range itself; "src=LO:HI" adds
// one), words in [data_lo, data_hi) that point into the range (vtables, callback
// tables), and extra addresses given on the command line. Every other function starting in the range (fragments auto-analysis split off at
// branch targets) is removed first, then each entry is disassembled and made a function.
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript RebuildFunctions.java <lo> <hi> <data_lo> <data_hi> [extra ...]
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;

public class RebuildFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long lo = Long.decode(args[0]), hi = Long.decode(args[1]);
        long dataLo = Long.decode(args[2]), dataHi = Long.decode(args[3]);
        Memory mem = currentProgram.getMemory();
        TreeSet<Long> entries = new TreeSet<>();
        List<long[]> sources = new ArrayList<>();
        sources.add(new long[] {lo, hi});
        for (int i = 4; i < args.length; i++) {
            if (args[i].startsWith("src=")) {
                String[] r = args[i].substring(4).split(":");
                sources.add(new long[] {Long.decode(r[0]), Long.decode(r[1])});
            } else {
                entries.add(Long.decode(args[i]));
            }
        }
        // jal targets in the source ranges.
        for (long[] src : sources) {
            for (long a = src[0]; a + 4 <= src[1]; a += 4) {
                int w;
                try {
                    w = mem.getInt(toAddr(a));
                } catch (Exception ex) {
                    continue;
                }
                if ((w >>> 26) != 3) continue;
                long t = ((a + 4) & 0xF0000000L) | ((long) (w & 0x3FFFFFF) << 2);
                if (t >= lo && t < hi) entries.add(t);
            }
        }
        for (long a = dataLo; a + 4 <= dataHi; a += 4) {
            long v = mem.getInt(toAddr(a)) & 0xFFFFFFFFL;
            if (v < lo || v >= hi || (v & 3) != 0) continue;
            // Only addresses where a function can start (after "jr ra" + delay slot, or
            // padding after it): switch tables also point into the code.
            long p = v - 8;
            while (p >= lo && mem.getInt(toAddr(p + 4)) == 0 && mem.getInt(toAddr(p)) == 0) p -= 4;
            if (v == lo || (p >= lo && mem.getInt(toAddr(p)) == 0x03e00008)) entries.add(v);
        }
        List<Function> remove = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(toAddr(lo), true)) {
            long a = f.getEntryPoint().getOffset();
            if (a >= hi) break;
            remove.add(f);
        }
        for (Function f : remove) removeFunction(f);
        int created = 0;
        for (long a : entries) {
            Address at = toAddr(a);
            new DisassembleCommand(at, null, true).applyTo(currentProgram, monitor);
        }
        for (long a : entries) {
            Address at = toAddr(a);
            if (getFunctionAt(at) != null) continue;
            if (new CreateFunctionCmd(at).applyTo(currentProgram, monitor)) created++;
        }
        println("ENTRIES " + entries.size() + " CREATED " + created + " (removed " + remove.size() + ")");
    }
}
