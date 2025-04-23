#include <iostream>
#include <fstream>
#include <unordered_map>
#include <cstdlib>
#include "pin.H"
#include <array>
#include <queue>

using std::cerr;
using std::endl;
using std::ios;
using std::ofstream;
using std::string;
using std::array;

#define STOP_INSTR_NUM 1024
#define SIMULATOR_HEARTBEAT_INSTR_NUM 100
#define CONTEXT_SIZE 4
#define MAX_TABLE_SIZE 1024

class HybridLoadValuePredictor {
private:
    // structure for storing the VHT entries and index
    struct VHTEntry_context {
      array<UINT64, CONTEXT_SIZE> values;
      int nextIndex;        // at what position in the array next actual value needs to be stored 
    };
  
    // declare a value history table with last 4 values assigned to one PC 
    std::unordered_map<ADDRINT, VHTEntry_context> valueHistoryTable_context;

    //value predict table to predcit the final value of the load
    std::unordered_map<ADDRINT, UINT64> valuePredictTable_context;

    // queue to implement the FIFO replacement policy in the table 
    std::queue<ADDRINT> insertionQueue_context;

    // map to store the confidence counter for each predictor
    std::unordered_map<std::string, int> confidenceTable;

    // stores the last used predictor name
    std::string lastUsedPredictor;
 

    void replacementPolicy(UINT64 max_table_size, std::unordered_map<ADDRINT, UINT64> &table,    
                            std::queue<ADDRINT> &fifo_queue, ADDRINT hashIndex, UINT64 actualValue) {
        // if the given hash index does not exist in the table then evict upon exceeding the table size
        if (table.find(hashIndex) == table.end()) {
            if (table.size() >= max_table_size) {
                ADDRINT evictHashIndex = fifo_queue.front();
                fifo_queue.pop();
                UINT64 evictedValue_context = table[evictHashIndex];
                table.erase(evictHashIndex);    // FIFO replacement policy
                std::cerr << "[Evict] PC: 0x" << std::hex << evictHashIndex 
                            << ", Old Value: 0x" << evictedValue_context 
                            << " | New Entry -> PC: 0x" << hashIndex << std::dec
                            << ", Value: 0x" << actualValue << std::endl;
            }
            fifo_queue.push(hashIndex);    // insert the new value at the end of the queue
        }
    }


public:
    HybridLoadValuePredictor() {
        confidenceTable["context"] = 0;
        confidenceTable["stride"] = 0;
        confidenceTable["storeToLoad"] = 0;
        confidenceTable["none"] = 0;
        lastUsedPredictor = "";
    }

    UINT64 getPrediction(ADDRINT loadPC, ADDRINT memoryAddr) {
        // if the context predictor has the highest confidence value
        if ((confidenceTable["context"] > confidenceTable["stride"]) && 
            (confidenceTable["context"]) > confidenceTable["storeToLoad"]) {    
            if (valueHistoryTable_context.find(loadPC) != valueHistoryTable_context.end()) {
                // generate the hash value to index in the value prediction table
                ADDRINT hashFun = 0;
                for (int i = 0; i < CONTEXT_SIZE; i++) {
                    hashFun = valueHistoryTable_context[loadPC].values[i] ^ hashFun;  // XOR hash function 
                }
                if (valuePredictTable_context.find(hashFun) != valuePredictTable_context.end()) {
                    lastUsedPredictor = "context";
                    return valuePredictTable_context[hashFun];  // Return the value from value predict table
                }
            }
        }

        lastUsedPredictor = "none";
        return 0;       // Default prediction
    } 
  
    // add the value to the value predict table and value histoy table on misprediction
    // or in case where the loadPC does not exist in the value history table 
    void train(ADDRINT loadPC, UINT64 actualValue) {
        if (valueHistoryTable_context.find(loadPC) != valueHistoryTable_context.end()) {

            VHTEntry_context& entry = valueHistoryTable_context[loadPC];
            
            // add the latest actual value to the valueHistoryTable_context
            entry.values[entry.nextIndex] = actualValue;

            // if the index has reached the value of 3, reset it to 0
            // else increment it by 1
            entry.nextIndex = (entry.nextIndex + 1) % CONTEXT_SIZE;

            // generate the hash value to index in the value prediction table
            UINT64 hashFun_train = 0;
            for (int i = 0; i < CONTEXT_SIZE; i++) {
                hashFun_train = entry.values[i] ^ hashFun_train;  // XOR hash function
            }
            // check if size of the valuePredictTable is full and perform FIFO replacement
            replacementPolicy(MAX_TABLE_SIZE, valuePredictTable_context, insertionQueue_context, hashFun_train, actualValue);

            // assign actual value in the VPT
            valuePredictTable_context[hashFun_train] = actualValue;
        }
        // if the entry does not exist in the valueHistoryTable already
        else {
            // infinite size valueHistoryTable
            valueHistoryTable_context[loadPC].values[0] = actualValue;
            for (int i = 1; i < CONTEXT_SIZE; i++) {
                valueHistoryTable_context[loadPC].values[i] = 0;
            }
            valueHistoryTable_context[loadPC].nextIndex = 1;    // updating the location in array to store next actual value

            // check if size of the valuePredictTable is full and perform FIFO replacement
            // finite size = 1024 valuePredictTable
            replacementPolicy(MAX_TABLE_SIZE, valuePredictTable_context, insertionQueue_context, loadPC, actualValue);

            valuePredictTable_context[loadPC] = actualValue;   
        }
    }

    // function to calculate the penalty
    void confidenceUpdate(UINT64 actualValue, UINT64 predictedValue) {
        if (predictedValue == actualValue) {
            // increment the confidence counter for the predictor by 1
            confidenceTable[lastUsedPredictor]++;   
        }
        // calculate the penalty in case of missprediction (penalty of -7)
        else {
            if (confidenceTable[lastUsedPredictor] > 7) {
                confidenceTable[lastUsedPredictor] -= 7;
            }
            else {
                confidenceTable[lastUsedPredictor] = 0;
            }
        }
    }
};

HybridLoadValuePredictor *hybridLoadValuePredictor;

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

    UINT64 predictedValue = hybridLoadValuePredictor->getPrediction(loadPC, memoryAddr);

    std::cerr << "PC: " << std::hex << loadPC << " | Predicted: " 
    << predictedValue << " | Actual: " << actualValue << std::dec << std::endl;

    if (predictedValue == actualValue) {
        correctPredictionCount++;
    }
    // calculate the penalty in case of missprediction
    hybridLoadValuePredictor->confidenceUpdate(actualValue, predictedValue);
    hybridLoadValuePredictor->train(loadPC, actualValue);
    
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

    hybridLoadValuePredictor = new HybridLoadValuePredictor();
    
    std::cerr << "Running context-based load value predictor simulation..." << std::endl;
    std::cerr << "Max Instructions: " << STOP_INSTR_NUM << std::endl;

    OutFile.open(KnobOutputFile.Value().c_str());

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();

    return 0;
}
