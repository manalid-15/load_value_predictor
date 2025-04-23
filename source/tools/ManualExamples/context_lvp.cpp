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
#define CONTEXT_SIZE 4

class LoadValuePredictor {
private:
    // structure for storing the VHT entries and index
    struct VHTEntry {
      array<UINT64, CONTEXT_SIZE> values;
      int nextIndex;
    };
  
    // declare a value history table with last 4 values assigned to one PC 
    std::unordered_map<ADDRINT, VHTEntry> valueHistoryTable;

    //value predict table to predcit the final value of the load
    std::unordered_map<ADDRINT, UINT64> valuePredictTable;

public:
    LoadValuePredictor() {}

    UINT64 getPrediction(ADDRINT loadPC) {
        if (valueHistoryTable.find(loadPC) != valueHistoryTable.end()) {
            // generate the hash value to index in the value prediction table
            ADDRINT hashFun = 0;
            for (int i = 0; i < CONTEXT_SIZE; i++) {
                hashFun = valueHistoryTable[loadPC].values[i] ^ hashFun;  // XOR hash function 
            }
            
            if (valuePredictTable.find(hashFun) != valuePredictTable.end()) {
                return valuePredictTable[hashFun];  // Return the value from value predict table
            }
	    }
	  
        return 0; // Default prediction
    }
  
    // add the value to the value predict table and value histoy table on misprediction
    // or in case where the loadPC does not exist in the value history table 
    void train(ADDRINT loadPC, UINT64 actualValue) {
        if (valueHistoryTable.find(loadPC) != valueHistoryTable.end()) {

            VHTEntry& entry = valueHistoryTable[loadPC];
            
            // add the latest actual value to the valueHistoryTable
            entry.values[entry.nextIndex] = actualValue;

            // if the index has reached the value of 3, reset it to 0
            // else increment it by 1
            entry.nextIndex = (entry.nextIndex + 1) % CONTEXT_SIZE;

            // generate the hash value to index in the value prediction table
            UINT64 hashFun_train = 0;
            for (int i = 0; i < CONTEXT_SIZE; i++) {
                hashFun_train = entry.values[i] ^ hashFun_train;  // XOR hash function
            }

            // assign actual value in the VPT
            valuePredictTable[hashFun_train] = actualValue;
        }

        else {
            valueHistoryTable[loadPC].values[0] = actualValue;
            for (int i = 1; i < CONTEXT_SIZE; i++) {
                    valueHistoryTable[loadPC].values[i] = 0;
            }
            valueHistoryTable[loadPC].nextIndex = 1;
            valuePredictTable[loadPC] = actualValue;
        }
    }
};

LoadValuePredictor *loadValuePredictor;

ofstream OutFile;

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "context_lvp.out", "specify output file name");

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

    UINT64 predictedValue = loadValuePredictor->getPrediction(loadPC);

    std::cerr << "PC: " << std::hex << loadPC << " | Predicted: " 
    << predictedValue << " | Actual: " << actualValue << std::dec << std::endl;

    if (predictedValue == actualValue) {
        correctPredictionCount++;
    }

    loadValuePredictor->train(loadPC, actualValue);
    
    totalPredictions++;
}

// Use memory address to manually fetch values instead of IARG_REG_VALUE
VOID Instruction(INS ins, VOID *v) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);

    if (INS_IsMemoryRead(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AtLoadInstruction,
                        IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);
    }
}

INT32 Usage() {
    cerr << "This tool simulates a context based load value predictor for load instructions." << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

int main(int argc, char * argv[]) {
    if (PIN_Init(argc, argv)) return Usage();

    loadValuePredictor = new LoadValuePredictor();
    
    std::cerr << "Running context-based load value predictor simulation..." << std::endl;
    std::cerr << "Max Instructions: " << STOP_INSTR_NUM << std::endl;

    OutFile.open(KnobOutputFile.Value().c_str());

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();

    return 0;
}
