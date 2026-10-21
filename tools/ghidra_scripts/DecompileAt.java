// Decompile the functions at the given addresses to OUTDIR/<address>_<name>.c,
// first creating a function at any address that has none (code reached only
// through vtables or function-pointer tables is not found by auto-analysis).
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript DecompileAt.java <outdir> <addr> [<addr> ...]
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path out = Path.of(args[0]);
        Files.createDirectories(out);
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);
        for (int i = 1; i < args.length; i++) {
            Address a = toAddr(Long.decode(args[i]));
            Function f = getFunctionAt(a);
            if (f == null) {
                Function inside = getFunctionContaining(a);
                if (inside != null) removeFunction(inside);  // mis-merged body: split it
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
                f = getFunctionAt(a);
                if (inside != null && getFunctionAt(inside.getEntryPoint()) == null)
                    new CreateFunctionCmd(inside.getEntryPoint()).applyTo(currentProgram, monitor);
            }
            if (f == null) {
                println("NO FUNCTION at " + a);
                continue;
            }
            DecompileResults r = decomp.decompileFunction(f, 120, monitor);
            String c = r.decompileCompleted() && r.getDecompiledFunction() != null
                    ? r.getDecompiledFunction().getC()
                    : "/* decompile failed: " + r.getErrorMessage() + " */\n";
            Files.writeString(out.resolve(String.format("%08x_%s.c", a.getOffset(), f.getName())), c);
            println("DECOMPILED " + f.getName());
        }
        decomp.dispose();
    }
}
