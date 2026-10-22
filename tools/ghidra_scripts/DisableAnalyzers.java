// Pre-script: turns off the given auto-analysis options (e.g. "PsyQ Signatures", the slow
// one) for the program being imported.
// Headless: analyzeHeadless <proj> <name> -import <file> -preScript DisableAnalyzers.java "<option>" ...
import ghidra.app.script.GhidraScript;

public class DisableAnalyzers extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String name : getScriptArgs()) {
            setAnalysisOption(currentProgram, name, "false");
            println("disabled " + name);
        }
    }
}
