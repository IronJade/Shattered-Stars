// Decompile every function whose entry lies in [lo, hi) to OUTDIR/<address>_<name>.c
// and write OUTDIR/index.csv (address, name, size, callers, callees).
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis -readOnly
//           -postScript DecompileAll.java <outdir> <lo> <hi>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path out = Path.of(args[0]);
        long lo = Long.decode(args[1]), hi = Long.decode(args[2]);
        Files.createDirectories(out);
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);
        int done = 0, failed = 0;
        try (PrintWriter index = new PrintWriter(out.resolve("index.csv").toFile())) {
            index.println("address,name,size,callers,callees");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                long entry = f.getEntryPoint().getOffset();
                if (entry < lo || entry >= hi) continue;
                DecompileResults r = decomp.decompileFunction(f, 120, monitor);
                String c;
                if (r.decompileCompleted() && r.getDecompiledFunction() != null) {
                    c = r.getDecompiledFunction().getC();
                    done++;
                } else {
                    c = "/* decompile failed: " + r.getErrorMessage() + " */\n";
                    failed++;
                }
                Files.writeString(out.resolve(String.format("%08x_%s.c", entry, f.getName())), c);
                index.printf("0x%08x,%s,%d,%d,%d%n", entry, f.getName(), f.getBody().getNumAddresses(),
                        f.getCallingFunctions(monitor).size(), f.getCalledFunctions(monitor).size());
            }
        }
        decomp.dispose();
        println("DECOMPILED " + done + " failed " + failed + " -> " + out);
    }
}
