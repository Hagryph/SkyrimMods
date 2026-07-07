// FindTargetRefs.java - find code/data references to target addresses, including
// RIP-relative instruction operands that Ghidra may not expose as references.
// args: <outfile> <hexaddr> [<hexaddr> ...]
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
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

public class FindTargetRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: FindTargetRefs <outfile> <hexaddr> [<hexaddr> ...]");
            return;
        }

        Map<Long, Address> targets = new LinkedHashMap<>();
        for (int i = 1; i < args.length; i++) {
            long value = Long.parseLong(args[i].replace("0x", ""), 16);
            targets.put(value, toAddr(value));
        }

        PrintWriter pw = new PrintWriter(new FileWriter(args[0]));
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        Set<Function> funcs = new LinkedHashSet<>();

        pw.println("== reference-manager refs ==");
        for (Address target : targets.values()) {
            pw.println("// target " + target);
            ReferenceIterator refs = rm.getReferencesTo(target);
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Function f = getFunctionContaining(ref.getFromAddress());
                pw.println("  " + ref.getFromAddress() + " " + ref.getReferenceType()
                    + (f != null ? (" in 0x" + f.getEntryPoint() + " " + f.getName()) : ""));
                if (f != null) funcs.add(f);
            }
        }

        pw.println();
        pw.println("== rip-relative instruction refs ==");
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
            if (!targets.containsKey(resolved)) continue;
            Function f = getFunctionContaining(ins.getAddress());
            pw.println("  target " + targets.get(resolved) + " <- " + ins.getAddress()
                + " " + ins + (f != null ? (" in 0x" + f.getEntryPoint() + " " + f.getName()) : ""));
            if (f != null) funcs.add(f);
        }

        pw.println();
        pw.println("== data u64 refs ==");
        for (Address target : targets.values()) {
            pw.println("// target " + target);
            scanU64(mem, target.getOffset(), pw);
        }

        pw.println();
        pw.println("== decompiled refs ==");
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.openProgram(currentProgram);
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

        pw.close();
        println("FindTargetRefs: wrote " + args[0] + " functions=" + funcs.size());
    }

    private void scanU64(Memory mem, long value, PrintWriter pw) {
        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized() || !block.isLoaded()) continue;
            if (!block.getName().toLowerCase().contains("rdata") && !block.getName().toLowerCase().contains("data")) {
                continue;
            }
            Address start = block.getStart();
            Address end = block.getEnd();
            for (long off = start.getOffset(); off + 8 <= end.getOffset() + 1 && !monitor.isCancelled(); off++) {
                if (readU64(mem, toAddr(off)) == value) {
                    pw.println("  " + toAddr(off) + " block=" + block.getName());
                }
            }
        }
    }

    private long readU64(Memory mem, Address addr) {
        byte[] b = new byte[8];
        try {
            mem.getBytes(addr, b);
        } catch (Exception e) {
            return -1;
        }
        return (b[0] & 0xffL) | ((b[1] & 0xffL) << 8) | ((b[2] & 0xffL) << 16)
            | ((b[3] & 0xffL) << 24) | ((b[4] & 0xffL) << 32) | ((b[5] & 0xffL) << 40)
            | ((b[6] & 0xffL) << 48) | ((b[7] & 0xffL) << 56);
    }
}
