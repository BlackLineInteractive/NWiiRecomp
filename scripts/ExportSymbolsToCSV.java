// Exports symbols to CSV
// @category WiiRecomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.io.File;

public class ExportSymbolsToCSV extends GhidraScript {

    @Override
    protected void run() throws Exception {
        if (currentProgram == null) {
            println("No open program.");
            return;
        }

        File file = askFile("Select Output CSV", "Save");
        if (file == null) {
            return;
        }

        PrintWriter writer = new PrintWriter(file);
        writer.println("Name,Location,Size");

        SymbolTable symbolTable = currentProgram.getSymbolTable();
        SymbolIterator symbols = symbolTable.getSymbolIterator();

        while (symbols.hasNext()) {
            if (monitor.isCancelled()) {
                break;
            }
            Symbol sym = symbols.next();
            if (sym.getSymbolType() == SymbolType.FUNCTION || sym.getSymbolType() == SymbolType.LABEL) {
                String name = sym.getName();
                long addr = sym.getAddress().getOffset();
                
                long size = 0;
                if (sym.getSymbolType() == SymbolType.FUNCTION) {
                    FunctionManager fm = currentProgram.getFunctionManager();
                    Function func = fm.getFunctionAt(sym.getAddress());
                    if (func != null) {
                        size = func.getBody().getNumAddresses();
                    }
                }
                
                // Print in uppercase hex, padded to 8 chars
                String addrHex = String.format("%08X", addr);
                writer.println(name + "," + addrHex + "," + size);
            }
        }
        
        writer.close();
        println("Successfully exported symbols to: " + file.getAbsolutePath());
    }
}
