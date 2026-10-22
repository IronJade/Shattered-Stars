// Creates functions in the gaps of a code range [lo, hi): every address right after a
// "jr ra" and its delay slot (or lo itself) that no function contains and that holds a
// nonzero word becomes a function entry. Repeats until nothing new. For overlays whose
// functions are reached only through tables auto-analysis does not follow.
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript CreateGapFunctions.java <lo> <hi>
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class CreateGapFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long lo = Long.decode(args[0]), hi = Long.decode(args[1]);
        Memory mem = currentProgram.getMemory();
        int total = 0;
        for (int pass = 0; pass < 20; pass++) {
            int created = 0;
            for (long a = lo; a < hi; a += 4) {
                boolean candidate = a == lo;
                if (!candidate && a >= lo + 8) {
                    int prev = mem.getInt(toAddr(a - 8));
                    candidate = prev == 0x03e00008;
                }
                if (!candidate) continue;
                Address at = toAddr(a);
                if (getFunctionContaining(at) != null) continue;
                // Skip alignment padding.
                long b = a;
                while (b < hi && mem.getInt(toAddr(b)) == 0) b += 4;
                if (b >= hi) continue;
                at = toAddr(b);
                if (getFunctionContaining(at) != null) continue;
                new DisassembleCommand(at, null, true).applyTo(currentProgram, monitor);
                if (new CreateFunctionCmd(at).applyTo(currentProgram, monitor)) created++;
            }
            total += created;
            println("pass " + pass + ": " + created);
            if (created == 0) break;
        }
        println("CREATED " + total);
    }
}
