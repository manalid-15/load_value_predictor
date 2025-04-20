#include <iostream>
#include <fstream>
#include <unordered_map>
#include <cstdlib>
#include "pin.H"
#include <array>

using std::cerr;
using std::endl;
using std::ios;
using std::ofstream;
using std::string;
using std::array;

#define STOP_INSTR_NUM 1024
#define SIMULATOR_HEARTBEAT_INSTR_NUM 100

class LoadValuePredictor {
private:

    // table to store the store value of memory locations
    std::unordered_map<ADDRINT, UINT64> storeValueTable;

public:
    LoadValuePredictor() {}

    // store the values for the memory locations of store instructions in the table
    VOID RecordStoreVal(ADDRINT addr, UINT64 val) {
        storeValueTable[addr] = val;
        std::cerr << std::hex<< "The value being stored is :" << storeValueTable[addr] << std::dec << std::endl;
    }

    UINT64 getPrediction(ADDRINT memoryAddr) {
        if (storeValueTable.find(memoryAddr) != storeValueTable.end()) {
            return storeValueTable[memoryAddr];
        }
        return 0; // Default prediction
    }
  
    // add the value to the value predict table and value histoy table on misprediction
    // or in case where the loadPC does not exist in the value history table 
    void train(ADDRINT memoryAddr, UINT64 actualValue) {
        storeValueTable[memoryAddr] = actualValue;
    }
};

LoadValuePredictor *loadValuePredictor;

ofstream OutFile;

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "store-load_lvp.out", "specify output file name");

static UINT64 iCount = 0;
static UINT64 correctPredictionCount = 0;
static UINT64 totalPredictions = 0;    

VOID docount() {
    iCount++;
    if (iCount % SIMULATOR_HEARTBEAT_INSTR_NUM == 0) {
        std::cerr << "Executed " << iCount << " instructions." << endl;
    }
    // if (iCount == STOP_INSTR_NUM) {
    //     PIN_Detach();
    // }
}

VOID TerminateSimulationHandler(VOID *v) {
    OutFile.setf(ios::showbase);
    OutFile.close();
    std::cerr << "\nSimulation reached " << STOP_INSTR_NUM << " instructions." << endl;
    std::cerr << "Prediction Accuracy: " 
              << ((totalPredictions > 0) ? (100.0 * correctPredictionCount / totalPredictions) : 0) 
              << "%" << endl;
}

VOID Fini(int code, VOID * v) {
    std::cerr << "\nSimulation reached " << STOP_INSTR_NUM << " instructions." << endl;
    std::cerr << "Prediction Accuracy: " 
              << ((totalPredictions > 0) ? (100.0 * correctPredictionCount / totalPredictions) : 0) 
              << "%" << endl;

    TerminateSimulationHandler(v);
}


// Read the actual loaded value from memory using PIN_SafeCopy
VOID AtLoadInstruction(ADDRINT loadPC, ADDRINT memoryAddr) {
    UINT64 actualValue = 0;

    // Safely copy the value from the memory address
    if (PIN_SafeCopy(&actualValue, reinterpret_cast<void*>(memoryAddr), sizeof(UINT64)) != sizeof(UINT64)) {
      std::cerr << "Memory read failed at address: " << std::hex << memoryAddr << std::dec << std::endl;
        return;
    }

    UINT64 predictedValue = loadValuePredictor->getPrediction(memoryAddr);

    std::cerr << "PC: " << std::hex << loadPC << " | Memory Address: " << memoryAddr << " | Predicted: " 
    << predictedValue << " | Actual: " << actualValue << std::dec << std::endl;

    if (predictedValue == actualValue) {
        correctPredictionCount++;
    }

    loadValuePredictor->train(memoryAddr, actualValue);
    
    totalPredictions++;
}

// wrapper function to store the store value in the table
VOID AtStoreInstruction(ADDRINT memoryAddr, UINT64 val) {
    // Check if the instruction is store instruction
    loadValuePredictor->RecordStoreVal(memoryAddr, val);
}

// Use memory address to manually fetch values instead of IARG_REG_VALUE
VOID Instruction(INS ins, VOID *v) {
    // std::string disasm = INS_Disassemble(ins);
    // std::cout << "Instruction: " << disasm << std::endl;

    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);

    // wrapper function get get prediction and store updated values in the lookup table
    if (INS_IsMemoryRead(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AtLoadInstruction,
                        IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);
    }

    // if the instruction is store instrution
    if (INS_IsMemoryWrite(ins)) {
        // if the store instruction is storing a value from a register
        if (INS_OperandIsReg(ins, 1)) {
            REG reg = INS_OperandReg(ins, 1);       // Get the register holding the value to be stored
            if (REG_valid(reg) && REG_is_gr(reg)) {
                 // wrapper function to store the store value in the table
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AtStoreInstruction,
                IARG_MEMORYWRITE_EA, IARG_REG_VALUE, reg, IARG_END);
            }
            // std::cerr << "Store value from register: " << reg << std::endl;
        }
        // if the store instruction is storing the value from immediate value
        else if (INS_OperandIsImmediate(ins, 1)) {
            ADDRINT immVal = INS_OperandImmediate(ins, 1);
            std::cerr << "Store value from immediate: " << immVal << std::endl;

            // wrapper function to store the store value in the table
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AtStoreInstruction,
            IARG_MEMORYWRITE_EA, IARG_ADDRINT, immVal, IARG_END);
        }
    }   
}

INT32 Usage() {
    cerr << "This tool simulates a load-store load value predictor for load instructions." << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

int main(int argc, char * argv[]) {
    if (PIN_Init(argc, argv)) return Usage();

    loadValuePredictor = new LoadValuePredictor();
    
    std::cerr << "Running load-store load value predictor simulation..." << std::endl;
    std::cerr << "Max Instructions: " << STOP_INSTR_NUM << std::endl;

    OutFile.open(KnobOutputFile.Value().c_str());

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();

    return 0;
}
