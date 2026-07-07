// FindStringRefs.java - find raw ASCII string hits, code refs, and decompile referencing functions.
// args: <outfile> <string-fragment> [<string-fragment> ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class FindStringRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: FindStringRefs <outfile> <string-fragment> [<string-fragment> ...]");
            return;
        }

        PrintWriter pw = new PrintWriter(new FileWriter(args[0]));
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.openProgram(currentProgram);

        for (int i = 1; i < args.length; i++) {
            String needle = args[i];
            pw.println();
            pw.println("############ STRING " + needle + " ############");
            List<Address> hits = findAscii(mem, needle);
            pw.println("// raw hits: " + hits.size());

            Set<Function> funcs = new LinkedHashSet<>();
            for (Address hit : hits) {
                pw.println("// string @ " + hit);
                collectReferenceManagerRefs(rm, hit, funcs, pw);
                collectRipRelativeInstructionRefs(listing, hit, funcs, pw);
            }

            pw.println("// decompiled function count: " + funcs.size());
            for (Function f : funcs) {
                pw.println();
                pw.println("// ---- 0x" + f.getEntryPoint() + " " + f.getName() + " ----");
                DecompileResults result = decompiler.decompileFunction(f, 180, monitor);
                if (result != null && result.decompileCompleted()) {
                    pw.println(result.getDecompiledFunction().getC());
                } else {
                    pw.println("// decompile failed: " + (result != null ? result.getErrorMessage() : "null"));
                }
            }
        }

        pw.close();
        println("FindStringRefs: wrote " + args[0]);
    }

    private List<Address> findAscii(Memory mem, String needle) {
        byte[] bytes = needle.getBytes(StandardCharsets.US_ASCII);
        List<Address> hits = new ArrayList<>();
        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized() || !block.isLoaded()) continue;
            Address start = block.getStart();
            Address end = block.getEnd();
            long max = end.getOffset() - bytes.length + 1;
            for (long off = start.getOffset(); off <= max && !monitor.isCancelled(); off++) {
                if (matches(mem, toAddr(off), bytes)) hits.add(toAddr(off));
            }
        }
        return hits;
    }

    private boolean matches(Memory mem, Address addr, byte[] bytes) {
        byte[] actual = new byte[bytes.length];
        try {
            mem.getBytes(addr, actual);
        } catch (Exception e) {
            return false;
        }
        for (int i = 0; i < bytes.length; i++) {
            if (actual[i] != bytes[i]) return false;
        }
        return true;
    }

    private void collectReferenceManagerRefs(ReferenceManager rm, Address target, Set<Function> funcs, PrintWriter pw) {
        ReferenceIterator refs = rm.getReferencesTo(target);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            Function f = getFunctionContaining(ref.getFromAddress());
            pw.println("//   ref @ " + ref.getFromAddress() + " " + ref.getReferenceType()
                + (f != null ? (" in 0x" + f.getEntryPoint() + " " + f.getName()) : ""));
            if (f != null) funcs.add(f);
        }
    }

    private void collectRipRelativeInstructionRefs(Listing listing, Address target, Set<Function> funcs, PrintWriter pw) {
        InstructionIterator it = listing.getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            byte[] bytes;
            try {
                bytes = ins.getBytes();
            } catch (Exception e) {
                continue;
            }
            if (bytes.length < 5) continue;
            int disp = (bytes[bytes.length - 4] & 0xff)
                | ((bytes[bytes.length - 3] & 0xff) << 8)
                | ((bytes[bytes.length - 2] & 0xff) << 16)
                | (bytes[bytes.length - 1] << 24);
            long resolved = ins.getAddress().getOffset() + bytes.length + disp;
            if (resolved != target.getOffset()) continue;
            Function f = getFunctionContaining(ins.getAddress());
            pw.println("//   rip-relative ref @ " + ins.getAddress() + " " + ins
                + (f != null ? (" in 0x" + f.getEntryPoint() + " " + f.getName()) : ""));
            if (f != null) funcs.add(f);
        }
    }
}
