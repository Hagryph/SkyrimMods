// GetVtable.java - dump N pointers of a vtable. args: <hexVtable> <count> [<hexVtable> <count> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class GetVtable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        for (int k = 0; k + 1 < a.length; k += 2) {
            long vt = Long.parseLong(a[k].replace("0x", ""), 16);
            int n = Integer.parseInt(a[k + 1]);
            println("== vtable 0x" + Long.toHexString(vt) + " ==");
            for (int i = 0; i < n; i++) {
                long ptr = getLong(toAddr(vt + (long) i * 8));
                Function f = getFunctionAt(toAddr(ptr));
                println(String.format("  [%d] +0x%02x -> 0x%x  %s", i, i * 8, ptr, f != null ? f.getName() : ""));
            }
        }
    }
}
