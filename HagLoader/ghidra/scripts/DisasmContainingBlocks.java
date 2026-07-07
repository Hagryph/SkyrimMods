// DisasmContainingBlocks.java - disassemble the CC-delimited block around each address.
// args: <outfile> <hexaddr> [<hexaddr> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmContainingBlocks extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DisasmContainingBlocks <outfile> <hexaddr> [<hexaddr> ...]");
            return;
        }

        PrintWriter pw = new PrintWriter(new FileWriter(args[0]));
        Memory mem = currentProgram.getMemory();
        for (int i = 1; i < args.length; i++) {
            Address hit = toAddr(Long.parseLong(args[i].replace("0x", ""), 16));
            Address start = findBlockStart(mem, hit);
            pw.println("== hit " + hit + " block " + start + " ==");
            Address p = start;
            int instructions = 0;
            while (p != null && instructions < 500 && !monitor.isCancelled()) {
                if (instructions > 0 && isCcRun(mem, p, 4)) break;
                Instruction ins = getInstructionAt(p);
                if (ins == null) {
                    disassemble(p);
                    ins = getInstructionAt(p);
                }
                if (ins == null) {
                    pw.println("  0x" + p + "  (no instruction)");
                    break;
                }
                byte[] b = ins.getBytes();
                StringBuilder hex = new StringBuilder();
                for (byte bb : b) hex.append(String.format("%02x", bb & 0xFF));
                String marker = ins.getAddress().equals(hit) ? " <== hit" : "";
                pw.println(String.format("  0x%s  %-42s %s%s", p, ins.toString(), hex, marker));
                p = p.add(ins.getLength());
                instructions++;
            }
            pw.println();
        }
        pw.close();
        println("DisasmContainingBlocks: wrote " + args[0]);
    }

    private Address findBlockStart(Memory mem, Address hit) {
        long current = hit.getOffset();
        long min = Math.max(current - 0x500, currentProgram.getMinAddress().getOffset());
        long lastCc = -1;
        for (long off = current - 1; off >= min && !monitor.isCancelled(); off--) {
            if (readByte(mem, toAddr(off)) == 0xcc) {
                lastCc = off;
                break;
            }
        }
        if (lastCc < 0) return hit;
        long start = lastCc + 1;
        while (start < current && readByte(mem, toAddr(start)) == 0xcc) start++;
        return toAddr(start);
    }

    private boolean isCcRun(Memory mem, Address addr, int count) {
        for (int i = 0; i < count; i++) {
            if (readByte(mem, addr.add(i)) != 0xcc) return false;
        }
        return true;
    }

    private int readByte(Memory mem, Address addr) {
        try {
            return mem.getByte(addr) & 0xff;
        } catch (Exception e) {
            return -1;
        }
    }
}
