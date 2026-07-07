// RTTIDecompileRefs.java - find RTTI vtable references for class-name fragments and
// decompile the functions that reference those vtables.
// args: <outfile> <typeNameFragment> [<typeNameFragment> ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
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
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class RTTIDecompileRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: RTTIDecompileRefs <outfile> <typeNameFragment> [<typeNameFragment> ...]");
            return;
        }

        PrintWriter pw = new PrintWriter(new FileWriter(args[0]));
        Listing listing = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Memory mem = currentProgram.getMemory();
        long imageBase = currentProgram.getImageBase().getOffset();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.openProgram(currentProgram);

        for (int i = 1; i < args.length; i++) {
            String needle = args[i];
            pw.println();
            pw.println("############ RTTI " + needle + " ############");
            Address stringAddress = findRttiString(listing, needle);
            if (stringAddress == null) {
                pw.println("// not found");
                continue;
            }

            Address typeDescriptor = stringAddress.subtract(0x10);
            pw.println("// string @ " + stringAddress);
            pw.println("// typeDescriptor @ " + typeDescriptor);

            Set<Address> vtables = new LinkedHashSet<>();
            collectVtablesFromGhidraRefs(rm, typeDescriptor, vtables, pw);
            collectVtablesFromGhidraRefs(rm, stringAddress, vtables, pw);
            collectVtablesFromRawRtti(mem, imageBase, typeDescriptor, vtables, pw);
            pw.println("// vtable count: " + vtables.size());

            Set<Function> funcs = new LinkedHashSet<>();
            for (Address vtable : vtables) {
                pw.println("// vtable @ " + vtable);
                ReferenceIterator refs = rm.getReferencesTo(vtable);
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    Function f = getFunctionContaining(ref.getFromAddress());
                    pw.println("//   vtable ref @ " + ref.getFromAddress()
                        + (f != null ? (" in 0x" + f.getEntryPoint() + " " + f.getName()) : ""));
                    if (f != null) funcs.add(f);
                }
                collectRipRelativeInstructionRefs(listing, vtable, funcs, pw);
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
        println("RTTIDecompileRefs: wrote " + args[0]);
    }

    private Address findRttiString(Listing listing, String needle) {
        Address fallback = null;
        DataIterator it = listing.getDefinedData(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Data data = it.next();
            Object value = data.getValue();
            if (value == null) continue;
            String text = value.toString();
            if (!text.contains(needle)) continue;
            if (fallback == null) fallback = data.getAddress();
            if (text.startsWith(".?A")) {
                return data.getAddress();
            }
        }
        return fallback;
    }

    private void collectVtablesFromGhidraRefs(ReferenceManager rm, Address target, Set<Address> out, PrintWriter pw) {
        ReferenceIterator refs = rm.getReferencesTo(target);
        while (refs.hasNext()) {
            Reference tdRef = refs.next();
            Address colBase = tdRef.getFromAddress().subtract(0x0c);
            ReferenceIterator colRefs = rm.getReferencesTo(colBase);
            while (colRefs.hasNext()) {
                Reference colRef = colRefs.next();
                Address vtable = colRef.getFromAddress().add(8);
                out.add(vtable);
                pw.println("// COL ref @ " + colRef.getFromAddress() + " => vtable @ " + vtable);
            }
        }
    }

    private void collectVtablesFromRawRtti(Memory mem, long imageBase, Address typeDescriptor,
            Set<Address> out, PrintWriter pw) {
        long tdRva = typeDescriptor.getOffset() - imageBase;
        List<Address> tdRvaHits = findU32(mem, tdRva);
        pw.println("// raw TypeDescriptor RVA hits: " + tdRvaHits.size());
        for (Address hit : tdRvaHits) {
            Address colBase = hit.subtract(0x0c);
            long selfRva = readU32(mem, colBase.add(0x14));
            pw.println("//   COL candidate " + colBase + " tdField=" + hit + " selfRva=0x"
                + Long.toHexString(selfRva));

            List<Address> colPtrHits = findU64(mem, colBase.getOffset());
            for (Address ptr : colPtrHits) {
                Address vtable = ptr.add(8);
                out.add(vtable);
                pw.println("//     raw COL ptr @ " + ptr + " => vtable @ " + vtable);
            }
        }
    }

    private void collectRipRelativeInstructionRefs(Listing listing, Address target, Set<Function> funcs,
            PrintWriter pw) {
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

    private List<Address> findU32(Memory mem, long value) {
        List<Address> hits = new ArrayList<>();
        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized() || !block.isLoaded()) continue;
            Address start = block.getStart();
            Address end = block.getEnd();
            for (long off = start.getOffset(); off + 4 <= end.getOffset() + 1 && !monitor.isCancelled(); off++) {
                Address addr = toAddr(off);
                if (readU32(mem, addr) == (value & 0xffffffffL)) hits.add(addr);
            }
        }
        return hits;
    }

    private List<Address> findU64(Memory mem, long value) {
        List<Address> hits = new ArrayList<>();
        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized() || !block.isLoaded()) continue;
            Address start = block.getStart();
            Address end = block.getEnd();
            for (long off = start.getOffset(); off + 8 <= end.getOffset() + 1 && !monitor.isCancelled(); off++) {
                Address addr = toAddr(off);
                if (readU64(mem, addr) == value) hits.add(addr);
            }
        }
        return hits;
    }

    private long readU32(Memory mem, Address addr) {
        byte[] b = new byte[4];
        try {
            mem.getBytes(addr, b);
        } catch (Exception e) {
            return -1;
        }
        return (b[0] & 0xffL) | ((b[1] & 0xffL) << 8) | ((b[2] & 0xffL) << 16) | ((b[3] & 0xffL) << 24);
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
