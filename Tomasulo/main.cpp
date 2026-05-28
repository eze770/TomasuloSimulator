#include<iostream>
#include<random>
#include<string>
#include<fstream>
#include<map>
#include<limits>

#ifndef Num_Rs_Alu
#define Num_Rs_Alu 4
#endif

#ifndef Num_Rs_Ls
#define Num_Rs_Ls 4
#endif

#ifndef Latency_AddSub
#define Latency_AddSub 1
#endif

#ifndef Latency_Mul
#define Latency_Mul 3
#endif

#ifndef Latency_Div
#define Latency_Div 5
#endif

#ifndef Latency_LS
#define Latency_LS 2
#endif

#ifndef Num_Regs
#define Num_Regs 8
#endif

#ifndef Num_Ram
#define Num_Ram 1024
#endif

enum OpCode {
    ADD, SUB, MUL,
    DIV, LOAD, STORE,
    NOP, END, S, V, H, I
};

enum RsType {
    lsType, aluType
};

struct Instruction {
    OpCode op;

    int dst;
    int src1; //Bei LOAD/STORE: src1 = Immediate
    int src2;
    RsType nopType;

    Instruction() : op(NOP), dst(-1), src1(-1), src2(-1), nopType(aluType){}
    Instruction(OpCode op, int dst, int src1, int src2)
        : op(op), dst(dst), src1(src1), src2(src2), nopType(aluType){ }
};

struct Register {
    int value;

    bool busy;
    std::pair<int, RsType> producerRS;
    Register() : value(-1), busy(false), producerRS({-1, lsType}) {}
};

struct ReservationStation {
    RsType rsType;
    bool busy; //something is in the rs (false after writeback)
    bool executing; //op in rs is executing (can still be busy) (true after execution)

    OpCode op;

    int Val1, Val2, src1, src2;          // Operationvalues, LOAD/STORE: Val1 = immediate, Val2 = address
    std::pair<int, RsType> Rs1, Rs2, dstRs;  // waiting RS, dstRs is only used for STORE
    int dst;

    //bool executing;
    //int remainingCycles;

    //int instructionId;

    ReservationStation()
        : rsType(lsType), busy(false), executing(false), op(NOP), Val1(0), Val2(0), src1(-1), src2(-1), Rs1({ -1, lsType }), Rs2({ -1, lsType }), dstRs({-1, lsType}), dst(0) { }
};

struct FunctionalUnit {
    bool busy;

    std::string rsName;

    int remainingCycles;
};

struct CDBMessage {
    bool valid;

    std::pair<int, RsType> producer;
    int value;

    CDBMessage() : valid(false), producer(-1, lsType), value(-1){}
};


int ram[Num_Ram];
Instruction instructionQueue[64];
Register reg[Num_Regs];
ReservationStation ls_rs[Num_Rs_Ls];
ReservationStation alu_rs[Num_Rs_Alu];
CDBMessage cdb[64];
int ic = 0;
int cdbIndex{ 0 };
int alubuffer{ -1 };
int lsbuffer{ -1 };
std::pair<int, RsType> oldAluProducerRs;
std::pair<int, RsType> oldLsProducerRs;
int aluDelay{ 0 };
int lsDelay{ 0 };
int lastAluStore{ 0 };
int lastLsStore{ 0 };
int lastRamDst{ -1 };
bool finished{ false };
bool AluRsCircled{ false };
bool LsRsCircled{ false };
int lastAluExecuted{ 0 };
int lastLsExecuted{ 0 };
OpCode lastLsOp{ STORE };
bool newLsEx{ false };
bool newAluEx{ false };
int fetchBalance{ 0 };
//Outputinformation:
int aluInstructions{ 0 };
int lsInstructions{ 0 };
int stallCycles{ 0 };
int fetchStallCycles{ 0 };
int rawPrevented{ 0 };
bool autoOutput{ false };



void alu(ReservationStation& rs, std::pair<int, RsType> prodRs, bool checkWb) {
    if (!checkWb)
    {
        if (aluDelay <= 0)
        {
            //std::cout << "\nExecuting: " << rs.op;
            rs.executing = true;

            switch (rs.op)
            {
            case ADD: { alubuffer = rs.Val1 + rs.Val2; aluDelay = Latency_AddSub; break; } //Takt
            case SUB: { alubuffer = rs.Val1 - rs.Val2; aluDelay = Latency_AddSub; break; } //Takt
            case MUL: { alubuffer = rs.Val1 * rs.Val2; aluDelay = Latency_Mul; break; } //Takt//Takt//Takt
            case DIV: {
                if (rs.Val2 == 0)
                {
                    std::cerr << "\nERROR: Division by zero || Returned 0!\n";
                    alubuffer = 0;
                }
                else
                {
                    alubuffer = rs.Val1 / rs.Val2;
                }
                aluDelay = Latency_Div; break;
            } //Takt//Takt//Takt//Takt//Takt
            case NOP: { rs.busy = false; break; } //So that we can save the last value
            case END: { finished = true; break; }
            default: break;
            }
            oldAluProducerRs = prodRs; //Remember it to write on it after the delay
            if (rs.op != END && rs.op != NOP)
            {
                aluInstructions++;
            }
            newAluEx = true;
        }
        else
        {
            newAluEx = false;
        }
    }
    else
    {
        if (aluDelay <= 0)
        {
            if (alubuffer != -1)
            {
                cdb[cdbIndex].value = alubuffer;
                cdb[cdbIndex].valid = true;
                cdb[cdbIndex].producer = oldAluProducerRs;
                cdbIndex++;
            }
        }
    }
}

void ls(ReservationStation& rs, std::pair<int, RsType> prodRs, bool checkWb) { //Mem phase included
    if (!checkWb)
    {
        if (lsDelay <= 0) {
            //std::cout << "\nExecuting: " << rs.op;
            rs.executing = true;

            switch (rs.op)
            {
            case LOAD: { lsbuffer = ram[rs.Val1 + rs.Val2]; lastLsOp = LOAD; break; } //Takt//Takt
            case STORE: { lastRamDst = rs.Val1 + reg[rs.Val2].value; lsbuffer = reg[rs.dst].value; reg[rs.dst].busy = false; rs.busy = false; rs.executing = true; lastLsOp = STORE; break; } //Takt//Takt
            case NOP: { rs.executing = true, rs.busy = false; lastLsOp = NOP; break; }
            default: break;
            }
            oldLsProducerRs = prodRs;
            if (rs.op != NOP)
            {
                lsInstructions++;
                lsDelay = Latency_LS;
            }
            newLsEx = true;
        }
        else
        {
            newLsEx = false;
        }
    }
    else
    {
        if (lsDelay <= 0)
        {
            if (lastLsOp == LOAD)
            {
                if (lsbuffer != -1)
                {
                    cdb[cdbIndex].value = lsbuffer;
                    cdb[cdbIndex].valid = true;
                    cdb[cdbIndex].producer = oldLsProducerRs;
                    cdbIndex++;
                }
            }
            else if (lastLsOp == STORE)
            {
                ram[lastRamDst] = lsbuffer;
            }
        }
    }    
} 

void showRegs() {
    std::cout << "\n\nREG: ";
    for (size_t i = 0; i <= sizeof(reg) / sizeof(reg[0]) - 1; i++)
    {
        std::cout << reg[i].value << " ";
    }
    std::cout << "\nRAM: ";
    for (size_t i = 0; i <= sizeof(ram) / sizeof(ram[0]) - 1; i++)
    {
        std::cout << ram[i] << " ";
    }
}

void initStorage(Register* reg) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255);
    for (size_t i = 0; i < Num_Regs; i++)
    {
        reg[i].value = distrib(gen);
    }
    for (size_t i = 0; i < Num_Ram; i++)
    {
        ram[i] = distrib(gen);
    }
    for (size_t i = 0; i < 63; i++)
    {
        instructionQueue[i].op = NOP;
    }
    for (size_t i = 0; i < Num_Rs_Alu; i++)
    {
        alu_rs[i].rsType = aluType;
    }
    for (size_t i = 0; i < Num_Rs_Ls; i++)
    {
        ls_rs[i].rsType = lsType;
    }
}

bool instructionFetchDecode() {
    ReservationStation* rs = nullptr;
    Instruction i = instructionQueue[ic];
    int l = 0;
    int j{ 0 };
    int it{ 0 };
    RsType rsType;
    /*if (i.op == NOP)
    {
        stallCycles++;
        return false;
    }*/
    if (i.op == STORE || i.op == LOAD || i.nopType == lsType) //RS auswählen //Takt
    {
        rs = ls_rs;
        rsType = lsType;
        it = Num_Rs_Ls - 1;
        if (lastLsStore == it) //Remembers last RS Store so that instructions are in Order
        {
            j = 0;
        }
        else
        {
            j = lastLsStore;
        }
    }
    else
    {
        rs = alu_rs;
        rsType = aluType;
        it = Num_Rs_Alu - 1;
        if (lastAluStore == it) //Remembers last RS Store so that instructions are in Order
        {
            j = 0;
        }
        else
        {
            j = lastAluStore;
        }
    }
    while (true)
    {
        if (rs[j].busy) //Freie RS finden //Takt
        {
            if (j < it)
            {
                j++;
            }
            else {
                if(i.op != I && i.op != H && i.op != S && i.op != V && i.op != END && i.op != NOP) fetchStallCycles++;
                return false;
            }
        }
        else
        {
            if (i.op != I && i.op != H && i.op != S && i.op != V && i.op != END && i.op != NOP)
            {
                fetchBalance++;
            }
            if (lastAluStore == it && rs->rsType == aluType && !rs[it].executing) AluRsCircled = true; //For the case that Rs is starting from the beginning again and execute needs to know where to continue
            if (lastLsStore == it && rs->rsType == lsType && !rs[it].executing) LsRsCircled = true;
            if (i.dst != -1 && i.op != STORE) { reg[i.dst].producerRS = { j, rsType }; }
            rs[j].busy = true;
            rs[j].executing = false;
            rs[j].op = i.op;
            rs[j].dst = i.dst;
            if (i.src1 != -1 && i.src2 != -1) //Some special commands like END use -1 and dont need values
            {
                if (rs->rsType == lsType) //Check if val1 needs to be offset or registervalue
                {
                    rs[j].Val1 = i.src1;
                    lastLsStore = j;
                    if (rs[j].op == STORE)
                    {
                        if (reg[i.dst].producerRS.first != -1) //STORE reads from its dst
                        {
                            rs[j].dstRs = reg[i.dst].producerRS;
                        }
                    }
                }
                else if (reg[i.src1].producerRS.first == -1 || i.dst == i.src1) //Check if value is available (could also write and read from same reg)
                {
                    rs[j].Val1 = reg[i.src1].value;
                    lastAluStore = j;
                }
                else
                {
                    rs[j].Rs1 = reg[i.src1].producerRS;
                    rs[j].src1 = i.src1;
                    lastAluStore = j;
                }

                if (reg[i.src2].producerRS.first == -1 || i.dst == i.src2) //Check if value is available (could also write and read from same reg)
                {
                    rs[j].Val2 = reg[i.src2].value;
                }
                else
                {
                    rs[j].Rs2 = reg[i.src2].producerRS;
                    rs[j].src2 = i.src2;
                }
            }
            else
            {
                lastAluStore = j;
            }
            break;
        }
    }
    ic++;
    if (i.op == V || i.op == H || i.op == I || i.op == S)
    {
        instructionFetchDecode();
    }
    return true;
}

void execute(int cycles) {
    int rsIndex{ 0 };
    bool done{ false };
    int start{ 0 };
    int cdbRead{ 0 };
    bool waitingRs{ false };
    ReservationStation temp;
    for (ReservationStation& rs : ls_rs)
    {
        if (LsRsCircled)
        {
            if (lastLsExecuted == Num_Rs_Ls - 1) lastLsExecuted = -1;
            start = rsIndex <= lastLsExecuted ? 0 : Num_Rs_Ls;
        }
        else
        {
            start = Num_Rs_Ls;
        }
        //std::cout << " LS: " << rs.op << rs.busy << rs.executing << start << " LL " << LsRsCircled << " " << lastLsStore;

        if (rs.busy && !rs.executing && rsIndex < start) //Check if execution is already beeing executed
        {
            waitingRs = true;
            for (CDBMessage& b : cdb)
            {
                if (b.producer == rs.Rs1 && rs.Rs1.first != -1) //Check if Value is in cdb
                {
                    rs.Val1 = b.value;
                    rs.Rs1.first = -1;
                    cdbRead++;
                }else if (reg[rs.src1].producerRS.first == -1)
                {
                    rs.Val1 = reg[rs.src1].value;
                }

                if (b.producer == rs.Rs2 && rs.Rs2.first != -1)
                {
                    rs.Val2 = b.value;
                    rs.Rs2.first = -1;
                    cdbRead++;
                }else if (reg[rs.src2].producerRS.first == -1)
                {
                    rs.Val2 = reg[rs.src2].value;
                }

                if (b.producer == rs.dstRs && rs.dstRs.first != -1)
                {
                    rs.dst = b.value;
                    rs.dstRs.first = -1;
                    cdbRead++;
                }else if (reg[rs.dst].producerRS.first == -1)
                {
                    rs.dst = reg[rs.dst].value;
                }

                if (rs.Rs1.first == -1 && rs.Rs2.first == -1 && rs.dstRs.first == -1)
                {
                    ls(rs, { rsIndex, rs.rsType }, false);
                    if(newLsEx)lastLsExecuted = lastLsExecuted == Num_Rs_Ls ? 0 : rsIndex;
                    done = true;
                    rawPrevented += cdbRead;
                    break;
                }
            }
        }
        rsIndex++;
        if (done)
        {
            if (LsRsCircled && rs.executing)
            {
                LsRsCircled = rsIndex == Num_Rs_Ls - 1 ? false : true;
            }
            break;
        }
    }
    if (!done)
    {
        temp.op = NOP;
        ls(temp, { -1, lsType }, false); //For the case that we have a RAW, this activates the WB (necessary for my implementation)
    }
    if (waitingRs) fetchBalance--;

    rsIndex = 0;
    done = false;
    start = 0;
    cdbRead = 0;
    waitingRs = false;
    ReservationStation aluTemp;
    for (ReservationStation& r : alu_rs)
    {
        if (AluRsCircled)
        {
            if (lastAluExecuted == Num_Rs_Alu - 1) lastAluExecuted = -1;
            start = rsIndex <= lastAluExecuted ? 0 : Num_Rs_Alu;
        }
        else
        {
            start = Num_Rs_Alu;
        }
        //std::cout << " ALU: " << r.op << r.busy << r.executing << start << " LL " << AluRsCircled << " " << lastAluExecuted;

        if (r.busy && !r.executing && rsIndex < start) //Check if execution is already beeing executed
        {
            if (r.op != END && r.op != V && r.op != H && r.op != I && r.op != S) waitingRs = true;
            for (CDBMessage& b : cdb)
            {
                if (b.producer == r.Rs1 && r.Rs1.first != -1) //Check if Value is in cdb
                {
                    r.Val1 = b.value;
                    r.Rs1.first = -1;
                    cdbRead++;
                }else if (reg[r.src1].producerRS.first == -1)
                {
                    r.Val1 = reg[r.src1].value;
                    r.Rs1.first = -1;
                }
                if (b.producer == r.Rs2 && r.Rs2.first != -1)
                {
                    r.Val2 = b.value;
                    r.Rs2.first = -1;
                    cdbRead++;
                }else if (reg[r.src2].producerRS.first == -1)
                {
                    r.Val2 = reg[r.src2].value;
                }

                if (r.Rs1.first == -1 && r.Rs2.first == -1)
                {
                    switch (r.op)
                    {
                    case S: if (!autoOutput) { showRegs(); }; r.busy = false; r.executing = false; break;
                    case H: if (!autoOutput) { std::cout << "Cycles: " << cycles << " IPC: " << double(lsInstructions + aluInstructions) / cycles; }; r.busy = false; r.executing = false; break;
                    case V: autoOutput = true; r.busy = false; r.executing = false; break;
                    case I: if (!autoOutput) { std::cout << " Stalls: " << stallCycles << " Structual stalls: " << fetchStallCycles << RAW prevented: " << rawPrevented; }; r.busy = false; r.executing = false; break;
                    case END: {
                        bool breaking{ false };
                        for (size_t i = 0; i < Num_Rs_Ls; i++)
                        {
                            if (ls_rs[i].busy && !ls_rs[i].executing && ls_rs[i].op != NOP ||lsDelay > 0)
                            {
                                breaking = true;
                            }
                        }
                        for (size_t i = 0; i < Num_Rs_Alu; i++)
                        {
                            if (alu_rs[i].busy && !alu_rs[i].executing && alu_rs[i].op != NOP && alu_rs[i].op != END || aluDelay > 0)
                            {
                                breaking = true;
                            }
                        }
                        for (size_t i = 0; i < sizeof(cdb) / sizeof(cdb[0]) - 1; i++)
                        {
                            if (cdb[i].valid)
                            {
                                breaking = true;
                            }
                        }
                        if (breaking)
                        {
                            break;
                        }
                        else
                        {
                            alu(r, { rsIndex, r.rsType }, false);
                            done = true;
                            break;
                        }
                    }
                    default:
                        alu(r, { rsIndex, r.rsType }, false);
                        if(newAluEx)lastAluExecuted = lastAluExecuted == Num_Rs_Alu ? 0 : rsIndex;
                        done = true;
                        rawPrevented += cdbRead;
                        break;
                    }
                    break;
                }
            }
        }
        rsIndex++;
        if (done)
        {
            if (AluRsCircled && r.executing)
            {
                AluRsCircled = rsIndex == Num_Rs_Alu ? false : true;
            }
            break;
        }
    }
    if (!done)
    {
        aluTemp.op = NOP;
        alu(aluTemp, { -1, aluType }, false); //For the case that we have a RAW, this activates the WB (necessary for my implementation)
    }
    if (waitingRs) fetchBalance--;

    //Counting StallCycles
    for (ReservationStation& rs : ls_rs)
    {
        if (rs.busy && !rs.executing && rs.op != H && rs.op != S && rs.op != V && rs.op != I && rs.op != END && rs.op != NOP)
        {
            stallCycles++;
        }
    }

    waitingRs = false;
    for (ReservationStation& rs : alu_rs)
    {
        if (rs.busy && !rs.executing && rs.op != H && rs.op != S && rs.op != V && rs.op != I && rs.op != END && rs.op != NOP)
        {
            stallCycles++;
        }
    }

    //I do this because my warmupphase makes its first execute in Cycle 3, which means that there could be 2 commands which are not executing. I dont want to count them as stalls
    if (fetchBalance > 0)
    {
        stallCycles -= fetchBalance;
    }

    //It is important to decrease the delay AFTER both ls and alu execution
    aluDelay--;
    alu(aluTemp, { -1, aluType }, true); //To write to cdb at the exact right moment

    lsDelay--;
    ls(temp, { -1, lsType }, true);
}

bool mem() { //Currently implemented in ls(), dont know if its necessary
    return true;
}

void writeBack() {
    bool waitingRs{ false };
    for (int i = 0; i < 64 ; i++)
    {
        if (cdb[i].valid)
        {
            ReservationStation* rs = cdb[i].producer.second == lsType ? ls_rs : alu_rs;
            if (reg[rs[cdb[i].producer.first].dst].producerRS == cdb[i].producer)
            {
                reg[rs[cdb[i].producer.first].dst].value = cdb[i].value;
                reg[rs[cdb[i].producer.first].dst].busy = true; //implemented it the wrong way (true means, the value is ready)
                reg[rs[cdb[i].producer.first].dst].producerRS.first = -1;
            }
            cdb[i].valid = false;
            rs[cdb[i].producer.first].busy = false;
            rs[cdb[i].producer.first].executing = false;
        }
    }
    cdbIndex = 0;
}
/// <summary>
/// Decodes the Inputprogramm which is a string to an correct inputformat.
/// Sorry for the following very ugly code D:
/// </summary>
void decodeProgramm() {
    std::string prgm;
    std::string line;
    std::cout << "Enter the name of the file which contains the programm: ";
    std::cin >> prgm;
    std::ifstream file(prgm);
    int lineIt{ 0 };
    int spaceCount{ 0 };
    if (!file)
    {
        std::cerr << "\nCould not open file" << std::endl;
        decodeProgramm();
        return;
    }
    while (std::getline(file, line))
    {
        int l{ 0 };
        int j{ 0 };
        line.append(" "); //So that the last Register is not beeing ignored
        for (char i : line)
        {
            if (i == ' ')
            {
                prgm = "";
                for (size_t n = j; n < l; n++)
                {
                    prgm.append(std::string(1, line[n]));
                }

                //std::cout << "prgm: " << prgm;
                if (prgm == "S" || prgm == "s")
                {
                    instructionQueue[lineIt].op = S;
                }
                else if (prgm == "V" || prgm == "v")
                {
                    instructionQueue[lineIt].op = V;
                }
                else if (prgm == "I" || prgm == "i")
                {
                    instructionQueue[lineIt].op = I;
                }
                else if (prgm == "H" || prgm == "h")
                {
                    instructionQueue[lineIt].op = H;
                }
                else if (prgm == "ADD")
                {
                    instructionQueue[lineIt].op = ADD;
                }
                else if (prgm == "SUB")
                {
                    instructionQueue[lineIt].op = SUB;
                }
                else if (prgm == "MUL")
                {
                    instructionQueue[lineIt].op = MUL;
                }
                else if (prgm == "DIV")
                {
                    instructionQueue[lineIt].op = DIV;
                }
                else if (prgm == "LOAD")
                {
                    instructionQueue[lineIt].op = LOAD;
                }
                else if (prgm == "STORE")
                {
                    instructionQueue[lineIt].op = STORE;
                }
                else if (prgm == "NOP")
                {
                    instructionQueue[lineIt].op = NOP;
                }
                else if (prgm == "END")
                {
                    instructionQueue[lineIt].op = END;
                }
                else if (spaceCount == 0)
                {
                    std::cout << "\nAn ERROR occured while reading programm";
                    lineIt--;
                }

                if (spaceCount == 1)
                {
                    instructionQueue[lineIt].dst = prgm[1] - '0'; //-'0' for convertion to int
                }
                else if (spaceCount == 2 && l - j == 3)//Checking if src1 is offset or register
                {
                    instructionQueue[lineIt].src1 = prgm[1] - '0'; // No offset
                }
                else if (spaceCount == 2) //For the case that src1 is offset
                {
                    instructionQueue[lineIt].src1 = prgm[0] - '0';
                    instructionQueue[lineIt].src2 = prgm[3] - '0';
                }
                else if (spaceCount == 3 && prgm[0] != '(')
                {
                    instructionQueue[lineIt].src2 = prgm[1] - '0';
                }

                spaceCount++;
                j = l + 1;
            }
            l++;
        }
        lineIt++;
        spaceCount = 0;
    }
    file.close();
    instructionQueue[lineIt].op = END;
}


int main() { //Programm/Register während Ablauf visualisieren + mgl. rückwärtsschritte zu machen
    int inputCycles{ 0 };
    int cycles{ 0 };

    initStorage(reg);
    decodeProgramm();
    showRegs();

    while (!finished)
    {
        std::cout << "\n\nEnter Cyclenumber: ";
        std::cin >> inputCycles;
        for (size_t i = 0; i < inputCycles; i++)
        {
            if (finished)
            {
                break;
            }
            switch (cycles)
            {
            case 0: instructionFetchDecode(); std::cout << "\nWarmup with Fetch" << std::endl; break;
            case 1: instructionFetchDecode(); std::cout << "\nWarmup with Decode" << std::endl; break;
            case 2: instructionFetchDecode(); execute(cycles); std::cout << "\nWarmup with Execute" << std::endl; break;
            case 3: instructionFetchDecode(); execute(cycles); mem(); std::cout << "\nWarmup with Mem" << std::endl; break;
            default:
                writeBack();
                instructionFetchDecode();
                execute(cycles);
                mem();
                std::cout << "\nFull pipeline-step" << std::endl;
                break;
            }
            cycles++;
            if (autoOutput)
            {
                showRegs();
                std::cout << "\n\nCycles: " << cycles << " IPC: " << double(lsInstructions + aluInstructions) / cycles
                    << " Stalls: " << stallCycles << " Structual stalls: " << fetchStallCycles << " RAW prevented: " << rawPrevented << " Auslastung ALU: "
                    << 100 * (aluInstructions / double(lsInstructions + aluInstructions)) << "% Auslastung L/S: "
                    << 100 * (lsInstructions / double(lsInstructions + aluInstructions)) << "%" << std::endl;
            }           
        }
    }
    std::cout << "\n\n---------------------------------------------------------------------\nProgramm finished!\n---------------------------------------------------------------------";
    std::cout << "\nZum Beenden Enter druecken...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
}
