// Give the CRT x87 helpers explicit ST0 parameters so their operands appear in the decompile.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.lang.Register;

public class FixX87 extends GhidraScript {
    void fix(long addr, String name, DataType ret) throws Exception {
        Function f = getFunctionAt(toAddr(addr));
        if (f == null) f = createFunction(toAddr(addr), name);
        Register st0 = currentProgram.getRegister("ST0");
        DataType f10 = new Float10DataType();
        Parameter p = new ParameterImpl("x", f10, st0, currentProgram);
        Variable r = new ReturnParameterImpl(ret, ret instanceof Float10DataType ? new VariableStorage(currentProgram, st0)
            : new VariableStorage(currentProgram, currentProgram.getRegister("EDX"), currentProgram.getRegister("EAX")), currentProgram);
        f.updateFunction("__cdecl", (ReturnParameterImpl) r, Function.FunctionUpdateType.CUSTOM_STORAGE, true,
            SourceType.USER_DEFINED, p);
        f.setName(name, SourceType.USER_DEFINED);
        println("fixed " + name);
    }
    @Override
    public void run() throws Exception {
        fix(0x444a98, "__ftol", new LongLongDataType());
        fix(0x444fb0, "x87_helper_444fb0", new Float10DataType());
    }
}
