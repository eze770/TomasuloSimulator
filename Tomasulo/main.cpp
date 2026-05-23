#include<iostream>
#include<random>
#include<string>
#include<fstream>
#include <map>

#ifndef Num_Rs_Alu
#define Num_Rs_Alu 4
#endif

#ifndef Num_Rs_Ls
#define Num_Rs_Ls 3
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

    Instruction() : op(NOP), dst(-1), src1(-1), src2(-1){}
    Instruction(OpCode op, int dst, int src1, int src2)
        : op(op), dst(dst), src1(src1), src2(src2){ }
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

    int Val1, Val2;          // Operandenwerte, Bei LOAD/STORE: Val1 = immediate, Val2 = address
    std::pair<int, RsType> Rs1, Rs2;  // wartende RS
    int dst;

    //bool executing;
    //int remainingCycles;

    //int instructionId;

    ReservationStation()
        : rsType(lsType), busy(false), executing(false), op(NOP), Val1(0), Val2(0), Rs1({-1, lsType}), Rs2({-1, lsType}), dst(0) { }
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
std::pair<int, RsType> oldAluProducerRs;
int aluDelay{ 0 };
int lsDelay{ 0 };
int lastAluStore{ 0 };
int lastLsStore{ 0 };
bool finished{ false };
bool AluRsCircled{ false };
bool LsRsCircled{ false };
//Outputinformation:
int aluInstructions{ 0 };
int lsInstructions{ 0 };
int stallCycles{ 0 };
int rawPrevented{ 0 };
bool autoOutput{ false };



void alu(ReservationStation& rs, std::pair<int, RsType> prodRs) {
    if (aluDelay == 0)
    {
        std::cout << "\nExecuting: " << rs.op;
        rs.executing = true;
        if (alubuffer != -1)
        {
            cdb[cdbIndex].value = alubuffer;
            cdb[cdbIndex].valid = true;
            cdb[cdbIndex].producer = oldAluProducerRs;
            cdbIndex++;
        }

        switch (rs.op)
        {
        case ADD: { alubuffer = rs.Val1 + rs.Val2; aluDelay = Latency_AddSub - 1; break; } //Takt
        case SUB: { alubuffer = rs.Val1 - rs.Val2; aluDelay = Latency_AddSub - 1; break; } //Takt
        case MUL: { alubuffer = rs.Val1 * rs.Val2; aluDelay = Latency_Mul - 1; break; } //Takt//Takt//Takt
        case DIV: { alubuffer = rs.Val1 / rs.Val2; aluDelay = Latency_Div - 1; break; } //Takt//Takt//Takt//Takt//Takt
        case NOP: { stallCycles++; rs.executing = true; rs.busy = false; break; } //So that we can save the last value
        case END: { finished = true; break; }
        default: break;
        }
        oldAluProducerRs = prodRs; //Remember it to write on it after the delay
        if (rs.op != NOP)
        {
            aluInstructions++;
        }
    }
    else
    {
        aluDelay--;
    }
}

void ls(ReservationStation& rs, std::pair<int, RsType> prodRs) { //Mem phase included
    if (lsDelay == 0){
        rs.executing = true;
        switch (rs.op)
        {
        case LOAD: { cdb[cdbIndex].value = ram[rs.Val1 + rs.Val2]; cdb[cdbIndex].valid = true; cdb[cdbIndex].producer = prodRs; cdbIndex++; break; } //Takt//Takt
        case STORE: { ram[rs.Val1 + reg[rs.Val2].value] = reg[rs.dst].value; reg[rs.dst].busy = false; rs.busy = false; rs.executing = false; break; } //Takt//Takt
        case NOP: { stallCycles++; rs.executing = true, rs.busy = false; break; }
        default: break;
        }
        lsDelay = Latency_LS - 1;
        if (rs.op != NOP)
        {
            lsInstructions++;
        }
    }
    else
    {
        lsDelay--;
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
    std::cout << "LAS: " << lastAluStore << " MAX: " << Num_Rs_Alu;
    RsType rsType;
    /*if (i.op == NOP)
    {
        stallCycles++;
        return false;
    }*/
    if (i.op == STORE || i.op == LOAD) //RS auswählen //Takt
    {
        rs = ls_rs;
        rsType = lsType;
        it = Num_Rs_Ls - 1;
        if (lastLsStore == it) //Remembers last RS Store so that instructions are in Order
        {
            j = 0;
            LsRsCircled = true;
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
            std::cout << "\nTRUE";
            j = 0;
            AluRsCircled = true;
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
                stallCycles++;
                return false;
            }
        }
        else
        {
            if (i.dst != -1) { reg[i.dst].producerRS = { j, rsType }; }
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
                }
                else if (reg[i.src1].producerRS.first == -1 || i.dst == i.src1) //Check if value is available (could also write and read from same reg)
                {
                    rs[j].Val1 = reg[i.src1].value;
                    lastAluStore = j;
                }
                else
                {
                    rs[j].Rs1 = reg[i.src1].producerRS;
                    lastAluStore = j;
                }

                if (reg[i.src2].producerRS.first == -1 || i.dst == i.src2) //Check if value is available (could also write and read from same reg)
                {
                    rs[j].Val2 = reg[i.src2].value;
                }
                else
                {
                    rs[j].Rs2 = reg[i.src2].producerRS;
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
    return true;
}

void execute(int cycles) {
    int rsIndex{ 0 };
    bool done{ false };
    int start{ 0 };
    for (ReservationStation& rs : ls_rs)
    {
        if (LsRsCircled)
        {
            start = rsIndex <= lastLsStore ? 0 : Num_Rs_Ls;
        }
        else
        {
            start = Num_Rs_Ls;
        }

        if (rs.busy && !rs.executing && rsIndex < start) //Check if execution is already beeing executed
        {
            for (CDBMessage& b : cdb)
            {
                if ((rs.Rs1.first == -1 || b.producer == rs.Rs1) && (rs.Rs2.first == -1 || b.producer == rs.Rs2))
                {
                    if (b.producer == rs.Rs1 && rs.Rs1.first != -1) //Check if Value is in cdb
                    {
                        rs.Val1 = b.value;
                        rawPrevented++;
                    }
                    if (b.producer == rs.Rs2 && rs.Rs2.first != -1)
                    {
                        rs.Val2 = b.value;
                        rawPrevented++;
                    }
                    break;
                }
                ls(rs, { rsIndex, rs.rsType });
                done = true;
            }
        }
        rsIndex++;
        if (done)
        {
            if (LsRsCircled)
            {
                LsRsCircled = rsIndex == Num_Rs_Ls - 1 ? false : true;
            }
            break;
        }
    }

    rsIndex = 0;
    done = false;
    start = 0;
    for (ReservationStation& r : alu_rs)
    {
        if (AluRsCircled)
        {
            start = rsIndex <= lastAluStore ? 0 : Num_Rs_Alu;
        }
        else
        {
            start = Num_Rs_Alu;
        }
        std::cout << " " << r.op << r.busy << r.executing << start << " LL " << AluRsCircled << " " << lastAluStore;

        if (r.busy && !r.executing && rsIndex < start) //Check if execution is already beeing executed
        {
            for (CDBMessage& b : cdb)
            {
                if ((r.Rs1.first == -1 || b.producer == r.Rs1) && (r.Rs2.first == -1 || b.producer == r.Rs2))
                {
                    if (b.producer == r.Rs1 && r.Rs1.first != -1) //Check if Value is in cdb
                    {
                        r.Val1 = b.value;
                        rawPrevented++;
                    }
                    if (b.producer == r.Rs2 && r.Rs2.first != -1)
                    {
                        r.Val2 = b.value;
                        rawPrevented++;
                    }
                    switch (r.op)
                    {
                    case S: if (!autoOutput) { showRegs(); }; r.busy = false; r.executing = false; break;
                    case H: if (!autoOutput) { std::cout << "Cycles: " << cycles << " IPC: " << double(lsInstructions + aluInstructions) / cycles; }; r.busy = false; r.executing = false; break;
                    case V: autoOutput = true; r.busy = false; r.executing = false; break;
                    case I: if (!autoOutput) { std::cout << " Stalls: " << stallCycles << " RAW prevented: " << rawPrevented; }; r.busy = false; r.executing = false; break;
                    case END: {
                        bool breaking{ false };
                        for (size_t i = 0; i < Num_Rs_Ls; i++)
                        {
                            if (ls_rs[i].busy)
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
                            alu(r, { rsIndex, r.rsType });
                            done = true;
                            break;
                        }
                    }
                    default:
                        alu(r, { rsIndex, r.rsType });
                        done = true;
                        break;
                    }
                    break;
                }
            }
            if (r.op != V && r.op != H && r.op != S && r.op != I && !done)
            {
                ReservationStation temp;
                temp.op = NOP;
                alu(temp, { -1, aluType }); //For the case that we have a RAW, this activates the WB (necessary for my implementation)
                done = true;
            }
        }
        rsIndex++;
        if (done)
        {
            std::cout << "SDFSDF" << AluRsCircled;
            if (AluRsCircled && r.executing)
            {
                AluRsCircled = rsIndex == Num_Rs_Alu ? false : true;
            }
            break;
        }
    }
}

bool mem() { //Currently implemented in ls(), dont know if its necessary
    return true;
}

void writeBack() {
    for (int i = 0; i < 64 ; i++)
    {
        if (cdb[i].valid)
        {
            ReservationStation* rs = cdb[i].producer.second == lsType ? ls_rs : alu_rs;
            if (reg[rs[cdb[i].producer.first].dst].producerRS == cdb[i].producer)
            {
                reg[rs[cdb[i].producer.first].dst].value = cdb[i].value;
                reg[rs[cdb[i].producer.first].dst].busy = true;
                reg[rs[cdb[i].producer.first].dst].producerRS.first = -1;
                cdb[i].valid = false;
                rs[cdb[i].producer.first].busy = false;
                rs[cdb[i].producer.first].executing = false;
            }
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

                std::cout << "prgm: " << prgm;
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
                else if (spaceCount == 0)
                {
                    std::cout << "\nAn ERROR occured while reading programm";
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
    instructionQueue[lineIt].op = NOP;
    instructionQueue[lineIt + 1].op = END;
}


int main() { //Programm/Register während Ablauf visualisieren + mgl. rückwärtsschritte zu machen
    int inputCycles{ 0 };
    int cycles{ 0 };

    initStorage(reg);
    decodeProgramm();
    showRegs();

    while (!finished)
    {
        std::cout << "\n\n+1 or +5 cycles? ";
        std::cin >> inputCycles;
        for (size_t i = 0; i < inputCycles; i++)
        {
            if (finished)
            {
                break;
            }
            switch (cycles)
            {
            case 0: instructionFetchDecode(); std::cout << "\nFetch" << std::endl; break;
            case 1: instructionFetchDecode(); std::cout << "\nFetch2" << std::endl; break;
            case 2: instructionFetchDecode(); execute(cycles); std::cout << "\nex" << std::endl; break;
            case 3: instructionFetchDecode(); execute(cycles); mem(); std::cout << "\nmem" << std::endl; break;
            default:
                instructionFetchDecode();
                execute(cycles);
                mem();
                writeBack();
                std::cout << "\nAll\n" << std::endl;
                break;
            }
            cycles++;
            if (autoOutput)
            {
                showRegs();
                std::cout << "\n\nCycles: " << cycles << " IPC: " << double(lsInstructions + aluInstructions) / cycles
                    << " Stalls: " << stallCycles << " RAW prevented: " << rawPrevented << " Auslastung ALU: "
                    << 100 * (aluInstructions / double(lsInstructions + aluInstructions)) << "% Auslastung L/S: "
                    << 100 * (lsInstructions / double(lsInstructions + aluInstructions)) << "%" << std::endl;
            }           
        }
    }
    std::cout << "\n\n---------------------------------------------------------------------\nProgramm finished!\n---------------------------------------------------------------------";
}
