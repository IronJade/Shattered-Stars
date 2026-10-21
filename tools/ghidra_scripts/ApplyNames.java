// Name functions from docs/catalog/functions.csv (columns module,address,name,...),
// keeping only rows for the given modules, or from "0xADDRESS NAME" lines.
// Creates a function where analysis missed one.
// Headless: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//           -postScript ApplyNames.java <functions.csv> <module[,module]>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        List<String> lines = Files.readAllLines(Path.of(args[0]));
        boolean csv = args[0].endsWith(".csv");
        Set<String> modules = args.length > 1 ? new HashSet<>(Arrays.asList(args[1].split(","))) : null;
        int renamed = 0;
        for (int i = 0; i < lines.size(); i++) {
            String line = lines.get(i).trim();
            if (line.isEmpty() || line.startsWith("#") || (csv && i == 0)) continue;
            String addrText, name;
            if (csv) {
                // module,address,name are always the first three plain fields.
                String[] cols = line.split(",", 4);
                if (cols.length < 3 || (modules != null && !modules.contains(cols[0]))) continue;
                addrText = cols[1];
                name = cols[2];
            } else {
                String[] parts = line.split("\\s+");
                if (parts.length < 2) continue;
                addrText = parts[0];
                name = parts[1];
            }
            Address addr = toAddr(Long.decode(addrText));
            Function f = getFunctionAt(addr);
            if (f == null) f = createFunction(addr, name);
            if (f == null) { printerr("no function at " + addr); continue; }
            f.setName(name, SourceType.USER_DEFINED);
            renamed++;
        }
        println("RENAMED " + renamed);
    }
}
