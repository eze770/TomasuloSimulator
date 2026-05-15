#include<iostream>
#include<random>

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
    bool busy;

    OpCode op;

    int Val1, Val2;          // Operandenwerte, Bei LOAD/STORE: Val1 = immediate, Val2 = address
    std::pair<int, RsType> Rs1, Rs2;  // wartende RS
    int dst;

    //bool executing;
    //int remainingCycles;

    //int instructionId;

    ReservationStation()
        : rsType(lsType), busy(false), op(NOP), Val1(0), Val2(0), Rs1({-1, lsType}), Rs2({-1, lsType}), dst(0) { }
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
        default: break;
        }
        oldAluProducerRs = prodRs; //Remember it to write on it after the delay
    }
    else
    {
        aluDelay--;
    }
}

void ls(ReservationStation& rs, std::pair<int, RsType> prodRs) { //Mem phase included
    if (lsDelay == 0)
    {
        switch (rs.op)
        {
        case LOAD: { cdb[cdbIndex].value = ram[rs.Val1 + rs.Val2]; cdb[cdbIndex].valid = true; cdb[cdbIndex].producer = prodRs; cdbIndex++; break; } //Takt//Takt
        case STORE: { ram[rs.Val1 + rs.Val2] = reg[rs.dst].value; reg[rs.dst].busy = false; break; } //Takt//Takt
        default: break;
        }
        lsDelay = 1;
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
        std::uniform_int_distribution<> distrib(0, 255);
        reg[i].value = distrib(gen);
        reg[i].busy = false;
        reg[i].producerRS = {-1, lsType};

        ram[i] = distrib(gen);

        ls_rs[i].rsType = lsType;
        alu_rs[i].rsType = aluType;
    }
}

bool instructionFetchDecode() {
    ReservationStation* rs = nullptr;
    Instruction i = instructionQueue[ic];
    int l = 0;
    int j{ 0 };
    RsType rsType;
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
                return false;
            }
        }
        else
        {
            reg[i.dst].producerRS = { j, rsType };
            rs[j].busy = true;
            rs[j].op = i.op;
            rs[j].dst = i.dst;
            if (reg[i.src1].producerRS.first == -1) //Prüfen, ob Werte verfügbar sind
            {
                rs[j].Val1 = reg[i.src1].value;
            }
            else
            {
                rs[j].Rs1 = reg[i.src1].producerRS;
            }

            if (reg[i.src2].producerRS.first == -1) //Prüfen, ob Werte verfügbar sind
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
    for (ReservationStation& rs : ls_rs)
    {
        if (rs.busy)
        {
            for (CDBMessage& b : cdb)
            {
                if ((rs.Rs1.first == -1 || b.producer == rs.Rs1) && (rs.Rs2.first == -1 || b.producer == rs.Rs2))
                {
                    rs.Val1 = (b.producer == rs.Rs1 && rs.Rs1.first != -1) ? b.value : rs.Val1; //Check if Value is in cdb
                    rs.Val2 = (b.producer == rs.Rs2 && rs.Rs2.first != -1) ? b.value : rs.Val2;
                    ls(rs, { rsIndex, rs.rsType });
                    break;
                }
            }
        }
        rsIndex++;
        break;
    }

    rsIndex = 0;
    for (ReservationStation& r : alu_rs)
    {
        if (r.busy)
        {
            for (CDBMessage& b : cdb)
            {
                if ((r.Rs1.first == -1 || b.producer == r.Rs1) && (r.Rs2.first == -1 || b.producer == r.Rs2))
                {
                    r.Val1 = (b.producer == r.Rs1 && r.Rs1.first != -1) ? b.value : r.Val1; //Check if Value is in cdb
                    r.Val2 = (b.producer == r.Rs2 && r.Rs2.first != -1) ? b.value : r.Val2;
                    alu(r, { rsIndex, r.rsType });
                    break;
                }
            }
        }
        rsIndex++;
        break;
    }
}

bool mem() { //Currently implemented in ls(), dont know if its necesarry
    return true;
}

void writeBack() {
    for (int i = cdbIndex - 1; i >= 0 ; i--)
    {
        if (cdb[i].valid)
        {
            for (size_t j = 0; j < 32; j++)
            {
                if (reg[j].producerRS == cdb[i].producer)
                {
                    reg[j].value = cdb[i].value;
                    reg[j].busy = true;
                    reg[j].producerRS.first = -1;
                    cdb[i].valid = false;
                }
            }
        }
    }
    cdbIndex = 0;
}


int main() { //Programm/Register während Ablauf visualisieren + mgl. rückwärtsschritte zu machen
    int inputCycles{ 0 };
    int cycles{ 0 };
    Instruction ins1(LOAD, 5, 1, 2); //hat random Wert gespeichert
    Instruction ins2(ADD, 4, 5, 2); //funzt
    Instruction ins3(SUB, 6, 7, 8); //macht gar nix

    initStorage(reg);

    instructionQueue[0] = ins1;
    instructionQueue[1] = ins2;
    for (size_t i = 2; i <= 31; i++)
    {
        instructionQueue[i] = ins3;
    }

    while (true)
    {
        for (size_t i = 0; i <= sizeof(reg) / sizeof(reg[0]) - 1; i++)
        {
            std::cout << reg[i].value << " ";
        }
        std::cout << "\n+1 or +5 cycles? ";
        std::cin >> inputCycles;
        for (size_t i = 0; i < inputCycles; i++)
        {
            switch (cycles)
            {
            case 0: instructionFetchDecode(); std::cout << "\nFetch\n"; break; // split it
            case 1: instructionFetchDecode(); std::cout << "\nFetch2\n"; break;
            case 2: instructionFetchDecode(); execute(); std::cout << "\nex\n"; break;
            case 3: instructionFetchDecode(); execute(); mem(); std::cout << "\nmem\n"; break;
            default:
                instructionFetchDecode();
                execute();
                mem();
                writeBack();
                std::cout << "\nAll\n";
                break;
            }
            cycles++;
        }
    }
}
