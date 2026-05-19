#include<iostream>
#include<random>
#include<string>
#include<fstream>

enum OpCode {
    ADD, SUB, MUL,
    DIV, LOAD, STORE,
    NOP
};

enum RsType {
    lsType, aluType
};

struct Instruction {
    OpCode op;

    int dst;
    int src1; //Bei LOAD/STORE: src1 = Immediate
    int src2;

    Instruction() : op(NOP), dst(0), src1(0), src2(0){}
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
    bool executed; //op in rs got executed (can still be busy) (true after execution)

    OpCode op;

    int Val1, Val2;          // Operandenwerte, Bei LOAD/STORE: Val1 = immediate, Val2 = address
    std::pair<int, RsType> Rs1, Rs2;  // wartende RS
    int dst;

    //bool executing;
    //int remainingCycles;

    //int instructionId;

    ReservationStation()
        : rsType(lsType), busy(false), executed(false), op(NOP), Val1(0), Val2(0), Rs1({-1, lsType}), Rs2({-1, lsType}), dst(0) { }
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


int ram[32];
Instruction instructionQueue[32];
Register reg[32];
ReservationStation ls_rs[32];
ReservationStation alu_rs[32];
CDBMessage cdb[32];
int ic = 0;
int cdbIndex{ 0 };
int alubuffer{ -1 };
std::pair<int, RsType> oldAluProducerRs;
int aluDelay{ 0 };
int lsDelay{ 0 };
//Outputinformation:
int aluInstructions{ 0 };
int lsInstructions{ 0 };
int stallCycles{ 0 };
int rawPrevented{ 0 };



void alu(ReservationStation& rs, std::pair<int, RsType> prodRs) {
    if (aluDelay == 0)
    {
        if (alubuffer != -1)
        {
            cdb[cdbIndex].value = alubuffer;
            cdb[cdbIndex].valid = true;
            cdb[cdbIndex].producer = oldAluProducerRs;
            cdbIndex++;
        }

        switch (rs.op)
        {
        case ADD: { alubuffer = rs.Val1 + rs.Val2; aluDelay = 0; break; } //Takt
        case SUB: { alubuffer = rs.Val1 - rs.Val2; aluDelay = 0; break; } //Takt
        case MUL: { alubuffer = rs.Val1 * rs.Val2; aluDelay = 2; break; } //Takt//Takt//Takt
        case DIV: { alubuffer = rs.Val1 / rs.Val2; aluDelay = 4; break; } //Takt//Takt//Takt//Takt//Takt
        case NOP: { break; } //So that we can save the last value
        default: break;
        }
        oldAluProducerRs = prodRs; //Remember it to write on it after the delay
        rs.executed = true;
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
        switch (rs.op)
        {
        case LOAD: { cdb[cdbIndex].value = ram[rs.Val1 + rs.Val2]; cdb[cdbIndex].valid = true; cdb[cdbIndex].producer = prodRs; cdbIndex++; break; } //Takt//Takt
        case STORE: { ram[rs.Val1 + reg[rs.Val2].value] = reg[rs.dst].value; reg[rs.dst].busy = false; break; } //Takt//Takt
        case NOP: { break; }
        default: break;
        }
        lsDelay = 1;
        rs.executed = true;
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

void initStorage(Register* reg) {
    for (size_t i = 0; i <= 31; i++)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, 15);
        reg[i].value = distrib(gen);
        reg[i].busy = false;
        reg[i].producerRS = {-1, lsType};

        ram[i] = distrib(gen);

        ls_rs[i].rsType = lsType;
        alu_rs[i].rsType = aluType;

        instructionQueue[i].op == NOP;
    }
}

bool instructionFetchDecode() {
    ReservationStation* rs = nullptr;
    Instruction i = instructionQueue[ic];
    int l = 0;
    int j{ 0 };
    RsType rsType;
    if (i.op == NOP)
    {
        stallCycles++;
        return false;
    }
    if (i.op == STORE || i.op == LOAD) //RS auswählen //Takt
    {
        rs = ls_rs;
        rsType = lsType;
    }
    else
    {
        rs = alu_rs;
        rsType = aluType;
    }
    while (true)
    {
        if (rs[j].busy) //Freie RS finden //Takt
        {
            if (j < 31)
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
            reg[i.dst].producerRS = { j, rsType };
            rs[j].busy = true;
            rs[j].executed = false;
            rs[j].op = i.op;
            rs[j].dst = i.dst;
            if (rs->rsType == lsType) //Check if val1 needs to be offset or registervalue
            {
                rs[j].Val1 = i.src1;
            }
            else if (reg[i.src1].producerRS.first == -1) //Check if value is available
            {
                rs[j].Val1 = reg[i.src1].value;
            }
            else
            {
                rs[j].Rs1 = reg[i.src1].producerRS;
            }

            if (reg[i.src2].producerRS.first == -1) //Check if value is available
            {
                rs[j].Val2 = reg[i.src2].value;
            }
            else
            {
                rs[j].Rs2 = reg[i.src2].producerRS;
            }
            break;
        }
    }
    ic++;
    return true;
}

void execute() {
    int rsIndex{ 0 };
    bool done{ false };
    for (ReservationStation& rs : ls_rs)
    {
        if (rs.busy && !rs.executed) //Check if execution is already beeing executed
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
                    ls(rs, { rsIndex, rs.rsType });
                    done = true;
                    break;
                }
            }
        }
        rsIndex++;
        if (done)
        {
            break;   
        }
    }

    rsIndex = 0;
    done = false;
    for (ReservationStation& r : alu_rs)
    {
        if (r.busy && !r.executed) //Check if execution is already beeing executed
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
                    alu(r, { rsIndex, r.rsType });
                    done = true;
                    break;
                }
            }
        }
        rsIndex++;
        if (done)
        {
            break;
        }
    }
    if (rsIndex == 32) //So that alu stallcycles decrease and last result is beeing stored
    {
        ReservationStation r;
        r.busy = true;
        r.executed = false;
        r.op = NOP;
        alu(r, { -1, aluType });
    }
}

bool mem() { //Currently implemented in ls(), dont know if its necessary
    return true;
}

void writeBack() {
    for (int i = 0; i < 32 ; i++)
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
                rs[cdb[i].producer.first].executed = false;
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
                if (prgm == "ADD")
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
                else if(spaceCount == 2 && l - j == 3)//Checking if src1 is offset or register
                {
                    instructionQueue[lineIt].src1 = prgm[1] - '0'; // No offset
                }
                else if (spaceCount == 2) //For the case that src1 is offset
                {
                    instructionQueue[lineIt].src1 = prgm[0] - '0';
                    instructionQueue[lineIt].src2 = prgm[3] - '0';
                }
                else if (spaceCount == 3)
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
}


int main() { //Programm/Register während Ablauf visualisieren + mgl. rückwärtsschritte zu machen
    int inputCycles{ 0 };
    int cycles{ 0 };

    initStorage(reg);
    decodeProgramm();
    //std::cout << "\nTestcode: " << instructionQueue[1].dst << " " << instructionQueue[1].src1 << " " << instructionQueue[1].src2;

    while (true)
    {
        std::cout << "\nREG: ";
        for (size_t i = 0; i <= sizeof(reg) / sizeof(reg[0]) - 1; i++)
        {
            std::cout << reg[i].value << " ";
        }
        std::cout << "\nRAM: ";
        for (size_t i = 0; i <= sizeof(reg) / sizeof(reg[0]) - 1; i++)
        {
            std::cout << ram[i] << " ";
        }
        std::cout << "\n+1 or +5 cycles? ";
        std::cin >> inputCycles;
        for (size_t i = 0; i < inputCycles; i++)
        {
            switch (cycles)
            {
            case 0: instructionFetchDecode(); std::cout << "\nFetch" << std::endl; break; // split it
            case 1: instructionFetchDecode(); std::cout << "\nFetch2" << std::endl; break;
            case 2: instructionFetchDecode(); execute(); std::cout << "\nex" << std::endl; break;
            case 3: instructionFetchDecode(); execute(); mem(); std::cout << "\nmem" << std::endl; break;
            default:
                instructionFetchDecode();
                execute();
                mem();
                writeBack();
                std::cout << "\nAll\n" << std::endl;
                break;
            }
            cycles++;
            std::cout << "\nCycles: " << cycles << " IPC: " << double(lsInstructions + aluInstructions) / cycles
                << " Stalls: " << stallCycles << " RAW prevented: " << rawPrevented << " Auslastung ALU: "
                << 100 * (aluInstructions / double(lsInstructions + aluInstructions)) << "% Auslastung L/S: "
                << 100 * (lsInstructions / double(lsInstructions + aluInstructions)) << "%" << std::endl;
        }
    }
}
