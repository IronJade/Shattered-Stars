// Removes the functions at the given addresses (blocks the compiler placed after a
// function's return that a recovery pass mistook for functions) and re-creates the
// owning function so its body follows the flow into them, then decompiles it.
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript MergeIntoFunction.java <outdir> <owner> <fragment> [<fragment> ...]
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;

public class MergeIntoFunction extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path out = Path.of(args[0]);
        Address owner = toAddr(Long.decode(args[1]));
        for (int i = 2; i < args.length; i++) {
            Function f = getFunctionAt(toAddr(Long.decode(args[i])));
            if (f != null) removeFunction(f);
        }
        Function o = getFunctionAt(owner);
        if (o != null) removeFunction(o);
        new CreateFunctionCmd(owner).applyTo(currentProgram, monitor);
        o = getFunctionAt(owner);
        println("owner body " + o.getBody().getNumAddresses() + " bytes");
        DecompInterface d = new DecompInterface();
        d.setOptions(new DecompileOptions());
        d.openProgram(currentProgram);
        DecompileResults r = d.decompileFunction(o, 300, monitor);
        Files.createDirectories(out);
        Files.writeString(out.resolve(String.format("%08x_%s.c", owner.getOffset(), o.getName())),
                r.decompileCompleted() ? r.getDecompiledFunction().getC() : "/* failed */");
        d.dispose();
    }
}
