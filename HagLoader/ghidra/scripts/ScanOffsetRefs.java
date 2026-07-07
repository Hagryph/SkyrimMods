// ScanOffsetRefs.java - scan instructions for field-offset operands.
// args: <outfile> <hexOffset> [<hexOffset> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class ScanOffsetRefs extends GhidraScript {
    private static final class Hit {
        String offset;
        String kind;
        Address address;
        String function;
        String instruction;
        String operands;
    }

    private static final class Counts {
        int total;
        int writes;
        int addressCalcs;
        int others;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: ScanOffsetRefs <outfile> <hexOffset> [<hexOffset> ...]");
            return;
        }

        long[] offsets = new long[args.length - 1];
        String[] labels = new String[args.length - 1];
        for (int i = 1; i < args.length; i++) {
            labels[i - 1] = args[i].toLowerCase(Locale.ROOT);
            offsets[i - 1] = Long.parseLong(labels[i - 1].replace("0x", ""), 16);
        }

        List<Hit> hits = new ArrayList<>();
        Map<String, Counts> byFunc = new LinkedHashMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            int n = ins.getNumOperands();
            for (int op = 0; op < n; op++) {
                String opText = ins.getDefaultOperandRepresentation(op);
                String compact = opText.toLowerCase(Locale.ROOT).replace(" ", "");
                for (int oi = 0; oi < offsets.length; oi++) {
                    if (!operandHasOffset(ins, op, compact, offsets[oi])) continue;
                    Hit hit = buildHit(ins, op, compact, labels[oi]);
                    hits.add(hit);
                    Counts c = byFunc.computeIfAbsent(hit.function, k -> new Counts());
                    c.total++;
                    if (hit.kind.equals("dest-memory-write")) c.writes++;
                    else if (hit.kind.equals("address-calc")) c.addressCalcs++;
                    else c.others++;
                }
            }
        }

        PrintWriter pw = new PrintWriter(new FileWriter(args[0]));
        pw.println("== field offset scan ==");
        for (int i = 0; i < offsets.length; i++) {
            pw.println("  " + labels[i] + " (" + offsets[i] + ")");
        }
        pw.println();
        pw.println("== functions ==");
        for (Map.Entry<String, Counts> e : byFunc.entrySet()) {
            Counts c = e.getValue();
            pw.println(e.getKey() + "  total=" + c.total + " writes=" + c.writes
                + " addressCalcs=" + c.addressCalcs + " other=" + c.others);
        }
        pw.println();
        pw.println("== hits ==");
        for (Hit h : hits) {
            pw.println(h.offset + " " + h.kind + " " + h.address + " " + h.function);
            pw.println("    " + h.instruction);
            pw.println("    operands: " + h.operands);
        }
        pw.close();
        println("ScanOffsetRefs: wrote " + args[0] + " hits=" + hits.size());
    }

    private boolean operandHasOffset(Instruction ins, int op, String compact, long offset) {
        String hx = "0x" + Long.toHexString(offset);
        if (compact.contains("+" + hx) || compact.contains("-" + hx)
            || compact.equals(hx) || compact.contains("[" + hx) || compact.contains("," + hx)) {
            return true;
        }
        for (Object obj : ins.getOpObjects(op)) {
            if (obj instanceof Scalar) {
                Scalar s = (Scalar)obj;
                long value = s.getUnsignedValue();
                if (value == offset) return true;
            }
        }
        return false;
    }

    private Hit buildHit(Instruction ins, int op, String compact, String label) {
        Hit h = new Hit();
        h.offset = label;
        h.address = ins.getAddress();
        Function f = getFunctionContaining(ins.getAddress());
        h.function = f != null ? ("0x" + f.getEntryPoint() + " " + f.getName()) : "(no function)";
        h.instruction = ins.toString();
        StringBuilder ops = new StringBuilder();
        for (int i = 0; i < ins.getNumOperands(); i++) {
            if (i != 0) ops.append(" | ");
            ops.append(i).append(":").append(ins.getDefaultOperandRepresentation(i));
        }
        h.operands = ops.toString();
        String mnemonic = ins.getMnemonicString().toUpperCase(Locale.ROOT);
        boolean memoryOperand = compact.contains("[") && compact.contains("]");
        boolean destination = op == 0;
        if (destination && memoryOperand && !mnemonic.startsWith("LEA")
            && !mnemonic.startsWith("CMP") && !mnemonic.startsWith("TEST")
            && !mnemonic.startsWith("UCOM") && !mnemonic.startsWith("COM")) {
            h.kind = "dest-memory-write";
        } else if (mnemonic.startsWith("LEA")) {
            h.kind = "address-calc";
        } else {
            h.kind = "other";
        }
        return h;
    }
}
