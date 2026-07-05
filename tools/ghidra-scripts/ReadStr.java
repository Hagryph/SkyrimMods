// ReadStr.java - print the C-string at each address. args: <hexAddr> ...
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class ReadStr extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String s : getScriptArgs()) {
            long a = Long.parseLong(s.replace("0x", ""), 16);
            Address ad = toAddr(a);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 96; i++) {
                byte b = getByte(ad.add(i));
                if (b == 0) break;
                sb.append((char) (b & 0xFF));
            }
            println(String.format("0x%x = \"%s\"", a, sb.toString()));
        }
    }
}
