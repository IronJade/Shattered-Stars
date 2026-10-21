// Creates functions at code addresses that are only referenced from data (C++ vtables,
// callback tables): every aligned word in [lo, hi) that points into [code_lo, code_hi),
// at an address no function contains yet, right after a "jr ra" + delay slot (or
// after an existing function's end), becomes a function. Repeats until nothing new.
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript CreatePointerFunctions.java <lo> <hi> <code_lo> <code_hi>
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.util.TreeSet;

public class CreatePointerFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long lo = Long.decode(args[0]), hi = Long.decode(args[1]);
        long codeLo = Long.decode(args[2]), codeHi = Long.decode(args[3]);
        Memory mem = currentProgram.getMemory();
        int total = 0;
        for (int pass = 0; pass < 8; ++pass) {
            TreeSet<Long> targets = new TreeSet<>();
            for (long a = lo; a + 4 <= hi; a += 4) {
                Address addr = toAddr(a);
                if (!mem.contains(addr) || !mem.getBlock(addr).isInitialized()) continue;
                // Skip words inside instructions of existing functions (code, not data).
                if (getInstructionAt(addr) != null) continue;
                long v = mem.getInt(addr) & 0xffffffffL;
                if (v < codeLo || v >= codeHi || (v & 3) != 0) continue;
                Address t = toAddr(v);
                if (getFunctionContaining(t) != null) continue;
                Address p2 = toAddr(v - 8);
                if (!mem.contains(p2) || !mem.getBlock(p2).isInitialized()) continue;
                long prev2 = mem.getInt(p2) & 0xffffffffL;
                if (prev2 != 0x03e00008L) continue;  // jr ra two words before
                targets.add(v);
            }
            int made = 0;
            for (long v : targets) {
                Address t = toAddr(v);
                if (getFunctionContaining(t) != null) continue;
                new DisassembleCommand(t, null, true).applyTo(currentProgram, monitor);
                if (new CreateFunctionCmd(t).applyTo(currentProgram, monitor)) made++;
            }
            println("pass " + pass + ": " + made + " functions");
            total += made;
            if (made == 0) break;
        }
        println("CREATED " + total);
    }
}
