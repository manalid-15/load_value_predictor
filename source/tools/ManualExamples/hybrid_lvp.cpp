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
#define COUNTER_THRESHOLD 3
#define PENALTY 2

class HybridLoadValuePredictor {
private:
    // structure for storing the VHT entries and index
    struct VHTEntry_context {
      array<UINT64, CONTEXT_SIZE> values;
      int nextIndex;        // at what position in the array next actual value needs to be stored 
    };  
    // structure for storing last values for stride predictor 
    struct StrideEntry {
        UINT64 lastValue;
        INT64 stride;
        bool valid;

        // structure constructor 
        StrideEntry() {
            lastValue = 0;
            stride = 0;
            valid = false;
        }
    };
  
    // declare a value history table with last 4 values assigned to one PC 
    std::unordered_map<ADDRINT, VHTEntry_context> valueHistoryTable_context;

    //value predict table to predcit the final value of the load
    std::unordered_map<ADDRINT, UINT64> valuePredictTable_context;

    // table to store the store value of memory locations
    std::unordered_map<ADDRINT, UINT64> storeValueTable;

    // table to store the values of stride 
    std::unordered_map<ADDRINT, StrideEntry> strideTable;

    // queue to implement the FIFO replacement policy in the table 
    std::queue<ADDRINT> insertionQueue_context;
    std::queue<ADDRINT> insertionQueue_storeToLoad;
    std::queue<ADDRINT> insertionQueue_stride;

    void replacementPolicy(UINT64 max_table_size, std::unordered_map<ADDRINT, UINT64> &table,    
                            std::queue<ADDRINT> &fifo_queue, ADDRINT hashIndex, UINT64 actualValue) {
        // if the given hash index does not exist in the table then evict upon exceeding the table size
        if (table.find(hashIndex) == table.end()) {
            if (table.size() >= max_table_size) {
                ADDRINT evictHashIndex = fifo_queue.front();
                fifo_queue.pop();
                // UINT64 evictedValue = table[evictHashIndex];
                table.erase(evictHashIndex);    // FIFO replacement policy
                // std::cerr << "[Evict] PC: 0x" << std::hex << evictHashIndex 
                //             << ", Old Value: 0x" << evictedValue 
                //             << " | New Entry -> PC: 0x" << hashIndex << std::dec
                //             << ", Value: 0x" << actualValue << std::endl;
            }
            fifo_queue.push(hashIndex);    // insert the new value at the end of the queue
        }
    }

    void replacementPolicy_stride(UINT64 max_table_size, std::unordered_map<ADDRINT, StrideEntry> &table,    
                                    std::queue<ADDRINT> &fifo_queue, ADDRINT hashIndex, UINT64 actualValue) {
        if (table.find(hashIndex) == table.end()) {
            if (table.size() >= max_table_size) {
                ADDRINT evictHashIndex = fifo_queue.front();
                fifo_queue.pop();
                // UINT64 evictedValue = table[evictHashIndex];
                table.erase(evictHashIndex);    // FIFO replacement policy
                // std::cerr << "[Evict] PC: 0x" << std::hex << evictHashIndex 
                //             << ", Old Value: 0x" << evictedValue 
                //             << " | New Entry -> PC: 0x" << hashIndex << std::dec
                //             << ", Value: 0x" << actualValue << std::endl;
            }
            fifo_queue.push(hashIndex);    // insert the new value at the end of the queue
        }
    }

public:
    HybridLoadValuePredictor() {
        confidenceTable["context"] = 0;
        confidenceTable["stride"] = 0;
        confidenceTable["storeToLoad"] = 0;
        store_counter = 0;
        context_counter = 0;
        stride_counter = 0;
    }

    UINT64 store_counter;
    UINT64 context_counter;
    UINT64 stride_counter;

    // map to store the confidence counter for each predictor
    std::unordered_map<std::string, int> confidenceTable;
    // function to get context predictor value
    UINT64 getPredictionContext(ADDRINT loadPC) {
        if (valueHistoryTable_context.find(loadPC) != valueHistoryTable_context.end()) {
            // generate the hash value to index in the value prediction table
            ADDRINT hashFun = 0;
            for (int i = 0; i < CONTEXT_SIZE; i++) {
                hashFun = valueHistoryTable_context[loadPC].values[i] ^ hashFun;  // XOR hash function 
            }
            if (valuePredictTable_context.find(hashFun) != valuePredictTable_context.end()) {
                return valuePredictTable_context[hashFun];  // Return the value from value predict table
            }
        }
        return 0;
    }
    // function to get store to load predictor value
    UINT64 getPredictionStoreToLoad(ADDRINT memoryAddr) {
        if (storeValueTable.find(memoryAddr) != storeValueTable.end()) {
            return storeValueTable[memoryAddr];
        }
        return 0;
    }

    UINT64 getPredictionStride(ADDRINT loadPC) {
        if (strideTable.find(loadPC) != strideTable.end()) {
            StrideEntry &strideEntry = strideTable[loadPC];
            UINT64 predictedValue = strideEntry.lastValue + strideEntry.stride;
            return predictedValue;
        }
        return 0;
    }
  
    // add the value to the value predict table and value histoy table on misprediction
    // or in case where the loadPC does not exist in the value history table 
    void train(ADDRINT loadPC, UINT64 actualValue, ADDRINT memoryAddr) {
        if (valueHistoryTable_context.find(loadPC) != valueHistoryTable_context.end()) {

            VHTEntry_context& contextEntry = valueHistoryTable_context[loadPC];
            
            // add the latest actual value to the valueHistoryTable_context
            contextEntry.values[contextEntry.nextIndex] = actualValue;

            // if the index has reached the value of 3, reset it to 0
            // else increment it by 1
            contextEntry.nextIndex = (contextEntry.nextIndex + 1) % CONTEXT_SIZE;

            // generate the hash value to index in the value prediction table
            UINT64 hashFun_train = 0;
            for (int i = 0; i < CONTEXT_SIZE; i++) {
                hashFun_train = contextEntry.values[i] ^ hashFun_train;  // XOR hash function
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

        // adding values in storeToLoad table
        // check the size of the table before adding an entry
        replacementPolicy(MAX_TABLE_SIZE, storeValueTable, insertionQueue_storeToLoad, memoryAddr, actualValue);
        storeValueTable[memoryAddr] = actualValue;

        // add values to strideTable and check the size of the table before adding 
        replacementPolicy_stride(MAX_TABLE_SIZE, strideTable, insertionQueue_stride, loadPC, actualValue);
        StrideEntry strideEntry;
        // for the entries of the loadPC other than the 1st entry
        if (strideEntry.valid) {
            // stride calculation
            strideEntry.stride = actualValue - strideEntry.lastValue;
        }
        strideEntry.lastValue = actualValue;
        strideEntry.valid = true;
        // add the value in the table
        strideTable[loadPC] = strideEntry;
    }

    // function to calculate the penalty
    void confidenceUpdate(UINT64 actualValue, UINT64 predictedValue, std::string predictor) {
        if (predictedValue == actualValue) {
            std::cout << "Increment confidence for " << predictor << " " << confidenceTable[predictor] << std::endl;
            // increment the confidence counter for the predictor by 1
            if (confidenceTable[predictor] < 15) {
                confidenceTable[predictor]++;   
            }
        }
        // calculate the penalty in case of missprediction
        else {
            if (confidenceTable[predictor] > PENALTY) {
                confidenceTable[predictor] -= PENALTY;
            }
            else {
                confidenceTable[predictor] = 0;
            }
        }
    }

    // store the values for the memory locations of store instructions in the table
    VOID RecordStoreVal(ADDRINT memoryAddr, UINT64 storeVal) {
        replacementPolicy(MAX_TABLE_SIZE, storeValueTable, insertionQueue_storeToLoad, memoryAddr, storeVal);
        storeValueTable[memoryAddr] = storeVal;
        // std::cerr << std::hex<< "The value being stored is :" << storeValueTable[addr] << std::dec << std::endl;
    }

    UINT64 finalPredictor(UINT64 predictedValue_context, UINT64 predictedValue_storeToLoad, UINT64 predictedValue_stride) {
        // if confidence of the context predictor is the highest
        if ((confidenceTable["context"] > confidenceTable["storeToLoad"]) && 
            (confidenceTable["context"] > confidenceTable["stride"]) && 
            (confidenceTable["context"] > COUNTER_THRESHOLD)) {
            context_counter++;
            return predictedValue_context;
        }
        else if ((confidenceTable["storeToLoad"] >= confidenceTable["context"]) && 
                 (confidenceTable["storeToLoad"] > confidenceTable["stride"]) &&     
                 (confidenceTable["storeToLoad"] > COUNTER_THRESHOLD)){
            store_counter++;
            return predictedValue_storeToLoad;
        } 
        // else use stride predictor
        else {
            stride_counter++;
            return predictedValue_stride;
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
        // std::cerr << "Executed " << iCount << " instructions." << endl;
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
    std::cout << "STORE counter: " << hybridLoadValuePredictor->store_counter << std::endl;
    std::cout << "CONTEXT counter: " << hybridLoadValuePredictor->context_counter << std::endl;
    std::cout << "STRIDE counter: " << hybridLoadValuePredictor->stride_counter << std::endl;
    std::cout << "CONTEXT confidence" << hybridLoadValuePredictor->confidenceTable["context"] << std::endl;
    std::cout << "STORE confidence" << hybridLoadValuePredictor->confidenceTable["storeToLoad"] << std::endl;
    std::cout << "STRIDE confidence" << hybridLoadValuePredictor->confidenceTable["stride"] << std::endl;
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

    UINT64 predictedValue_context = hybridLoadValuePredictor->getPredictionContext(loadPC);
    UINT64 predictedValue_storeToLoad = hybridLoadValuePredictor->getPredictionStoreToLoad(memoryAddr);
    UINT64 predictedValue_stride = hybridLoadValuePredictor->getPredictionStride(loadPC);

    // std::cerr << "PC: " << std::hex << loadPC << " | Predicted: " 
    // << predictedValue << " | Actual: " << actualValue << std::dec << std::endl;

    UINT64 finalPrediction = hybridLoadValuePredictor->finalPredictor(predictedValue_context, predictedValue_storeToLoad, predictedValue_stride);

    if (finalPrediction == actualValue) {
        correctPredictionCount++;
    }

    // calculate the penalty in case of missprediction
    hybridLoadValuePredictor->confidenceUpdate(actualValue, predictedValue_context, "context");
    hybridLoadValuePredictor->confidenceUpdate(actualValue, predictedValue_storeToLoad, "storeToLoad");
    hybridLoadValuePredictor->confidenceUpdate(actualValue, predictedValue_stride, "stride");

    hybridLoadValuePredictor->train(loadPC, actualValue, memoryAddr);
    
    totalPredictions++;
}

// wrapper function to store the store value in the table
VOID AtStoreInstruction(ADDRINT memoryAddr, UINT64 storeVal) {
    // Check if the instruction is store instruction
    hybridLoadValuePredictor->RecordStoreVal(memoryAddr, storeVal);
}

// Use memory address to manually fetch values instead of IARG_REG_VALUE
VOID Instruction(INS ins, VOID *v) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);

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
        }
        // if the store instruction is storing the value from immediate value
        else if (INS_OperandIsImmediate(ins, 1)) {
            ADDRINT immVal = INS_OperandImmediate(ins, 1);
            // std::cerr << "Store value from immediate: " << immVal << std::endl;

            // wrapper function to store the store value in the table
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AtStoreInstruction,
            IARG_MEMORYWRITE_EA, IARG_ADDRINT, immVal, IARG_END);
        }
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
    
    std::cerr << "Running hybrid load value predictor simulation..." << std::endl;
    // std::cerr << "Max Instructions: " << STOP_INSTR_NUM << std::endl;

    OutFile.open(KnobOutputFile.Value().c_str());

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();

    return 0;
}
