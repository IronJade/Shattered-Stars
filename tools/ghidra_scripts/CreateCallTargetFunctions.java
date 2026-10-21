// Makes every direct call target (jal / bal) in [lo, hi) a function entry. Auto-analysis
// sometimes merges a callee into the function before it (no "jr ra" between them), so
// the call shows up as func_0x... in the decompiler; this splits such bodies.
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript CreateCallTargetFunctions.java <lo> <hi>
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.FlowType;
import java.util.TreeSet;

public class CreateCallTargetFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long lo = Long.decode(args[0]), hi = Long.decode(args[1]);
        int total = 0;
        for (int pass = 0; pass < 10; ++pass) {
            TreeSet<Long> targets = new TreeSet<>();
            for (Instruction in : currentProgram.getListing().getInstructions(toAddr(lo), true)) {
                if (in.getAddress().getOffset() >= hi) break;
                FlowType ft = in.getFlowType();
                if (!ft.isCall()) continue;
                for (Address t : in.getFlows()) {
                    long v = t.getOffset();
                    if (v >= lo && v < hi && getFunctionAt(t) == null) targets.add(v);
                }
            }
            int made = 0;
            for (long v : targets) {
                Address t = toAddr(v);
                if (getFunctionAt(t) != null) continue;
                Function inside = getFunctionContaining(t);
                Address insideEntry = inside != null ? inside.getEntryPoint() : null;
                if (inside != null) removeFunction(inside);
                new DisassembleCommand(t, null, true).applyTo(currentProgram, monitor);
                if (new CreateFunctionCmd(t).applyTo(currentProgram, monitor)) made++;
                if (insideEntry != null && getFunctionAt(insideEntry) == null)
                    new CreateFunctionCmd(insideEntry).applyTo(currentProgram, monitor);
            }
            println("pass " + pass + ": " + made + " functions from " + targets.size() + " call targets");
            total += made;
            if (made == 0) break;
        }
        println("CREATED " + total);
    }
}
