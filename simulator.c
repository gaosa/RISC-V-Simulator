#include "simulator.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("please enter file name\n");
        return 0;
    } else if (argc == 3) {
        debug_mode = true;
        debug_type = NONE;
    } else if (argc > 3) {
        printf("too many arguments\n");
        return 0;
    } else {
        debug_mode = false;
    }
    
    // malloc the memory space
    mem = malloc(0x1000000);
    stack = malloc(0x1000010);
    if (mem == NULL || stack == NULL) {
        printf("out of memory\n");
        return 0;
    }    
    // setup the environment
    // based on elf
    setup(argv[argc - 1]);
    // execute ins by ins
    for (;;) {
        fetch();
        decode();
        debug_command();
        execute();
        memoryOp();
        writeback();
        change_pc();
    }
    
    free(mem);
    free(stack);
    return 0;
}


static inline ll FD2LL(double a) {
    ll b;
    memcpy(&b, &a, sizeof(double));
    return b;
}
static inline double LL2FD(ll a) {
    double b;
    memcpy(&b, &a, sizeof(ll));
    return b;
}
static inline int FW2W(float a) {
    int b;
    memcpy(&b, &a, sizeof(int));
    return b;
}
static inline float W2FW(int a) {
    float b;
    memcpy(&b, &a, sizeof(int));
    return b;
}
static inline void * DEST(ll offset) {
    if ((ull)offset < THRES) {
        return mem+(ull)offset;
    } else {
        return stack+(ull)(offset&0xffffff);
    }
}

static inline bool isdigits(char* str) {
    while (*str) {
        if (!isdigit(*str)) {
            return false;
        }
        ++str;
    }
    return true;
}

int setup(char *filename) {
    
    
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("%s doesn't exist.\n", filename);
        exit(0);
    }
    
    // get the size of the file
    fseek(file, 0, SEEK_END);
    ll size = ftell(file);
    
    // copy elf content into memory
    fseek(file, 0, 0);
    uchar *elf = (uchar *) malloc(size);
    if (elf == NULL) {
        printf("malloc failed\n");
        exit(0);
    }
    fread(elf, sizeof(uchar), size, file);
    fclose(file);
    
    // now elf has the content
    
    // print some basics
    
    // entry point
    ll e_entry = *(ll*)(elf+24);
    // set PC
    pc = e_entry;
    
    // header offset, size, and number
    ll e_phoff = *(ll*)(elf+32);
    short e_phentsize = *(short*)(elf+54);
    short e_phnum = *(short*)(elf+56);
    // printf("program header offset: 0x%llx\n", e_phoff);
    // printf("program header size: %hd\n", e_phentsize);
    // printf("program header number: %hd\n", e_phnum);
    
    
    // reading program header and load into memory
    for (int i = 0; i < e_phnum; ++i) {
        void* header_loc = elf + e_phoff + i * e_phentsize;
        
        // check if it's loadable
        int p_type = *(int*)header_loc;
        if (p_type != 1)
            continue;
        
        // offset, address and size
        ll p_offset = *(ll*)(header_loc+8);
        ll p_vaddr = *(ll*)(header_loc+16);
        ll p_memsz = *(ll*)(header_loc+40);
        // printf("load program header %d ...\n", i);
        // printf("load offset: 0x%llx\n", p_offset);
        // printf("load addr: 0x%llx\n", p_vaddr);
        // printf("load size: 0x%llx\n", p_memsz);
    
        // load into memory
        memcpy(mem+(ull)p_vaddr, elf+(ull)p_offset, p_memsz);
    }
    free(elf);

    // init zero
    reg[0] = 0;
    return 0;
}

static inline void fetch() {
    //printf("%llx:\t", pc);
    ins = *(uint*)(mem+(ull)pc);
    //printf("%08x\t", ins);
    //pc += 4;
}

void decode() {
    
    // init signal
    mem_write = mem_read = reg_write = pc_branch = false;

    // first, decode each part
    opcode = GET(6,0);
    rd     = GET(11,7);
    funct3 = GET(14,12);
    rs1    = GET(19,15);
    rs2    = GET(24,20);
    funct7 = GET(31,25);
    funct2 = GET(26,25);
    rs3    = GET(31,27);
    // imms
    i_imm  = GET(31,20);
    s_imm  = (GET(31,25)<<5)|
              GET(11,7);
    sb_imm = (GET(31,31)<<12)|
             (GET(30,25)<<5)|
             (GET(11,8)<<1)|
             (GET(7,7)<<11);
    u_imm  = GET(31,12)<<12;
    uj_imm = (GET(31,31)<<20)|
             (GET(30,21)<<1)|
             (GET(20,20)<<11)|
             (GET(19,12)<<12);


    // second, decide control signal
    switch (opcode) {
    // load
    case 0x3:
        alu_op = ADD;
        alu_op_1 = signExtImm12(i_imm);
        alu_op_2 = reg[rs1];
        mem_read = true;
        mem_reg = rd;
        switch (funct3) {
        case 0x0:
            sprintf(ins_str, "lb\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            mem_type = MEM_B;
            break;
        case 0x1:
            sprintf(ins_str, "lh\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            mem_type = MEM_H;
            break;
        case 0x2:
            sprintf(ins_str, "lw\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            mem_type = MEM_W;
            break;
        case 0x4:
            sprintf(ins_str, "lbu\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            mem_type = MEM_BU;
            break;
        case 0x5:
            sprintf(ins_str, "lhu\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            mem_type = MEM_HU;
            break;
        case 0x6:
            sprintf(ins_str, "lwu\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            mem_type = MEM_WU;
            break;
        case 0x3:
            sprintf(ins_str, "ldu\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            mem_type = MEM_D;
            break;
        default:
            printf("unknown ins: %x\n", ins);
            exit(0);
            break;
        }
        break;
    // double load data
    case 0x07:
        if (funct3 == 3) {
            sprintf(ins_str, "fld\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            alu_op = ADD;
            alu_op_1 = signExtImm12(i_imm);
            alu_op_2 = reg[rs1];
            mem_read = true;
            mem_reg = rd;
            mem_type = MEM_FD;

        } else {
            sprintf(ins_str, "flw\t%s,%lld(%s)", reg_name[rd], alu_op_1, reg_name[rs1]);
            alu_op = ADD;
            alu_op_1 = signExtImm12(i_imm);
            alu_op_2 = reg[rs1];
            mem_read = true;
            mem_reg = rd;
            mem_type = MEM_FW;
        }
        break;
    // ioperation
    case 0x13:
        alu_op_1 = reg[rs1];
        reg_write = true;
        reg_dest_rd = true;
        switch (funct3) {
        case 0x0:
            alu_op = ADD;
            alu_op_2 = signExtImm12(i_imm);
            sprintf(ins_str, "addi\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x2:
            alu_op = SLT;
            alu_op_2 = signExtImm12(i_imm);
            sprintf(ins_str, "slti\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x3:
            alu_op = SLTU;
            alu_op_2 = signExtImm12(i_imm);
            sprintf(ins_str, "sltiu\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x4:
            alu_op = XOR;
            alu_op_2 = signExtImm12(i_imm);
            sprintf(ins_str, "xori\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x6:
            alu_op = OR;
            alu_op_2 = signExtImm12(i_imm);
            sprintf(ins_str, "ori\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x7:
            alu_op = AND;
            alu_op_2 = signExtImm12(i_imm);
            sprintf(ins_str, "andi\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x1:
            alu_op = SLL;
            alu_op_2 = i_imm&0x3f;
            sprintf(ins_str, "slli\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x5:
            alu_op_2 = i_imm&0x3f;
            if (i_imm&0x400) {
                alu_op = SRA;
                sprintf(ins_str, "srai\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            } else {
                alu_op = SRL;
                sprintf(ins_str, "srli\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            }
            break;
        default:
            break;
        }
        break;
    // auipc
    case 0x17:
        alu_op = ADD;
        alu_op_1 = pc;
        alu_op_2 = u_imm;
        reg_write = true;
        reg_dest_rd = true;
        sprintf(ins_str, "auipc\t%s,0x%llx", reg_name[rd], alu_op_2);
        break;
    // wioperation
    case 0x1b:
        alu_op_1 = reg[rs1];
        reg_write = true;
        reg_dest_rd = true;
        switch (funct3) {
        case 0x0:
            alu_op = ADDW;
            alu_op_2 = signExtImm12(i_imm);
            sprintf(ins_str, "addiw\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x1:
            alu_op = SLLW;
            alu_op_2 = rs2;
            sprintf(ins_str, "slliw\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            break;
        case 0x5:
            if (funct7) {
                alu_op = SRAW;
                alu_op_2 = rs2;
                sprintf(ins_str, "sraiw\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            } else {
                alu_op = SRLW;
                alu_op_2 = rs2;
                sprintf(ins_str, "srliw\t%s,%s,%lld", reg_name[rd], reg_name[rs1], alu_op_2);
            }
            break;
        default:
            break;
        }
        break;
    // float store
    case 0x27:
        if (funct3 == 2) {
            alu_op = ADD;
            alu_op_1 = reg[rs1];
            alu_op_2 = signExtImm12(s_imm);
            mem_write = true;
            mem_type = MEM_FW;
            mem_reg = rs2;
            sprintf(ins_str, "fsw\t%s,%lld(%s)", reg_name[rs2], alu_op_2, reg_name[rs1]);
        } else {
            alu_op = ADD;
            alu_op_1 = reg[rs1];
            alu_op_2 = signExtImm12(s_imm);
            mem_write = true;
            mem_type = MEM_FD;
            mem_reg = rs2;
            sprintf(ins_str, "fsd\t%s,%lld(%s)", reg_name[rs2], alu_op_2, reg_name[rs1]);
        }
        break;
    // operation
    case 0x33:
        alu_op_1 = reg[rs1];
        alu_op_2 = reg[rs2];
        reg_write = true;
        reg_dest_rd = true;
        switch (funct3) {
        case 0:
            if (funct7&0x20) {
                alu_op = SUB;
                sprintf(ins_str, "sub\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else if (funct7&0x1) {
                alu_op = MUL;
                sprintf(ins_str, "mul\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else {
                alu_op = ADD;
                sprintf(ins_str, "add\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        case 0x1:
            if (funct7) {
                alu_op = MULH;
                sprintf(ins_str, "mulh\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else {
                alu_op = SLL;
                sprintf(ins_str, "sll\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        case 0x2:
            if (funct7) {
                alu_op = MULHSU;
                sprintf(ins_str, "mulhsu\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else {
                alu_op = SLT;
                sprintf(ins_str, "slt\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        case 0x3:
            if (funct7) {
                alu_op = MULHU;
                sprintf(ins_str, "mulhu\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else {
                alu_op = SLTU;
                sprintf(ins_str, "sltu\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        case 0x4:
            if (funct7) {
                alu_op = DIV;
                sprintf(ins_str, "div\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else {
                alu_op = XOR;
                sprintf(ins_str, "xor\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        case 0x5:
            if (funct7&0x20) {
                alu_op = SRA;
                sprintf(ins_str, "sra\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else if (funct7&0x1) {
                alu_op = DIVU;  
                sprintf(ins_str, "divu\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else {
                alu_op = SRL;
                sprintf(ins_str, "srl\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        case 0x6:
            if (funct7) {
                alu_op = REM;
                sprintf(ins_str, "rem\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else {
                alu_op = OR;
                sprintf(ins_str, "or\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        case 0x7:
            if (funct7) {
                alu_op = REMU;
                sprintf(ins_str, "remu\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);

            } else {
                alu_op = AND;
                sprintf(ins_str, "and\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            }
            break;
        default:
            printf("not: %x\n", ins);
            exit(0);
            break;
        }
        break;
    // store
    case 0x23:
        alu_op = ADD;
        alu_op_1 = signExtImm12(s_imm);
        alu_op_2 = reg[rs1];
        mem_write = true;
        mem_reg = rs2;
        switch (funct3) {
        case 0x0:
            mem_type = MEM_B;
            sprintf(ins_str, "sb\t%s,%lld(%s)", reg_name[rs2], alu_op_2, reg_name[rs1]);
            break;
        case 0x1:
            mem_type = MEM_H;
            sprintf(ins_str, "sh\t%s,%lld(%s)", reg_name[rs2], alu_op_2, reg_name[rs1]);
            break;
        case 0x2:
            mem_type = MEM_W;
            sprintf(ins_str, "sw\t%s,%lld(%s)", reg_name[rs2], alu_op_2, reg_name[rs1]);
            break;
        case 0x3:
            mem_type = MEM_D;
            sprintf(ins_str, "sd\t%s,%lld(%s)", reg_name[rs2], alu_op_2, reg_name[rs1]);
            break;
        default:
            break;
        }
        break;
    // lui
    case 0x37:
        reg_write = true;
        reg_dest_rd = true;
        alu_op = ADD;
        alu_op_1 = 0;
        alu_op_2 = u_imm;
        sprintf(ins_str, "lui\t%s,%llx", reg_name[rd], alu_op_2);
        break;
    // word operation
    case 0x3b:
        alu_op_1 = reg[rs1];
        alu_op_2 = reg[rs2];
        reg_write = true;
        reg_dest_rd = true;
        switch (funct3) {
        case 0x0:
            if (funct7&0x1) {
                alu_op = MULW;
                sprintf(ins_str, "mulw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            } else if (funct7&0x20) {
                sprintf(ins_str, "subw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                alu_op = SUBW;
            } else {
                sprintf(ins_str, "addw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                alu_op = ADDW;
            }
            break;
        case 0x1:
                sprintf(ins_str, "sllw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            alu_op = SLLW;
            break;
        case 0x4:
                sprintf(ins_str, "divu\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            alu_op = DIVW;
            break;
        case 0x5:
            if (funct7&0x1) {
                sprintf(ins_str, "divuw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                alu_op = DIVUW;
            } else if (funct7&0x20) {
                sprintf(ins_str, "sraw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                alu_op = SRAW;
            } else {
                sprintf(ins_str, "srlw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                alu_op = SRLW;
            }       
            break;
        case 0x6:
            sprintf(ins_str, "remw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            alu_op = REMW;
            break;
        case 0x7:
            sprintf(ins_str, "remuw\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            alu_op = REMUW;
            break;
        default:
            break;
        }
        break;
    // branch
    case 0x63:
        pc_dest = signExtImm13(sb_imm) + pc;
        switch (funct3) {
        case 0x0:
            sprintf(ins_str, "beq\t%s,%s,%llx", reg_name[rs1], reg_name[rs2], pc_dest);
            pc_branch = (reg[rs1] == reg[rs2]);
            break;
        case 0x1:
            sprintf(ins_str, "bne\t%s,%s,%llx", reg_name[rs1], reg_name[rs2], pc_dest);
            pc_branch = (reg[rs1] != reg[rs2]);
            break;
        case 0x4:
            sprintf(ins_str, "blt\t%s,%s,%llx", reg_name[rs1], reg_name[rs2], pc_dest);
            pc_branch = (reg[rs1] < reg[rs2]);
            break;
        case 0x5:
            sprintf(ins_str, "bge\t%s,%s,%llx", reg_name[rs1], reg_name[rs2], pc_dest);
            pc_branch = (reg[rs1] >= reg[rs2]);
            break;
        case 0x6:
            sprintf(ins_str, "bltu\t%s,%s,%llx", reg_name[rs1], reg_name[rs2], pc_dest);
            pc_branch = ((ull)reg[rs1] < (ull)reg[rs2]);
            break;
        case 0x7:
            sprintf(ins_str, "begeu\t%s,%s,%llx", reg_name[rs1], reg_name[rs2], pc_dest);
            pc_branch = ((ull)reg[rs1] >= (ull)reg[rs2]);
            break;
        default:
            printf("not: %x\n", ins);
            exit(0);
            break;
        }
        break;
    // jalr
    case 0x67:
        alu_op = ADD;
        alu_op_1 = pc;
        alu_op_2 = 4;
        reg_write = true;
        reg_dest_rd = true;
        pc_branch = true;
        pc_dest = signExtImm12(i_imm) + reg[rs1];
        sprintf(ins_str, "jalr\t%s,%llx", reg_name[rd], pc_dest);
        break;
    // jal
    case 0x6f:
        alu_op = ADD;
        alu_op_1 = pc;
        alu_op_2 = 4;
        pc_branch = true;
        pc_dest = pc + signExtImm21(uj_imm);
        reg_write = true;
        reg_dest_rd = true;
        sprintf(ins_str, "jal\t%s,%llx", reg_name[rd], pc_dest);
        break;
    // system call
    case 0x73:
        sprintf(ins_str, "ecall");
        switch (reg[17]) {
        case 64:
            reg[10] = (long)write((int)reg[10], (const void*)DEST(reg[11]), (size_t)reg[12]);
            break;
        case 63:
            reg[10] = read((int)reg[10], DEST(reg[11]), (size_t)reg[12]);
            break;
        case 93:
            exit((int)reg[10]);
            break;
        case 169:
            reg[10] = gettimeofday((struct timeval *)DEST(reg[10]), NULL);
            break;
        case 214:
            break;
        default:
            reg[10] = 0;
            break;
        }
        break;
    // float operation
    case 0x43:
        if (funct2 == 1) {
            f_reg[rd] = f_reg[rs1]*f_reg[rs2]+f_reg[rs3];
            sprintf(ins_str, "fmadd.d\t%s,%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2], reg_name[rs3]);
        } else {
            sprintf(ins_str, "fmadd.s\t%s,%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2], reg_name[rs3]);
            f_reg[rd] = f_reg[rs1]*f_reg[rs2]+f_reg[rs3];
        }
        break;
    // float operation
    case 0x53:
        switch (funct7) {
        case 0x1:
            sprintf(ins_str, "fadd.d\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            f_reg[rd] = f_reg[rs1] + f_reg[rs2];
            break;
        case 0x5:
            sprintf(ins_str, "fsub.d\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            f_reg[rd] = f_reg[rs1] - f_reg[rs2];
            break;
        case 0x9:
            sprintf(ins_str, "fmul.d\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            f_reg[rd] = f_reg[rs1] * f_reg[rs2];
            break;
        case 0xd:
            sprintf(ins_str, "fdiv.d\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            f_reg[rd] = f_reg[rs1] / f_reg[rs2];
            break;
        case 0x68:
            if (rs2 == 2) {
                f_reg[rd] = (double)reg[rs1];
                sprintf(ins_str, "fcvt.s.l\t%s,%s", reg_name[rd], reg_name[rs1]);
            } else if (rs2 == 0) {
                sprintf(ins_str, "fcvt.s.w\t%s,%s", reg_name[rd], reg_name[rs1]);
                f_reg[rd] = (double)((int)reg[rs1]);
            } else {
                printf("not set : %x\n", ins);
                exit(0);
            }
            break;
        case 0x69:
            if (rs2 == 0) {
                sprintf(ins_str, "fcvt.d.w\t%s,%s", reg_name[rd], reg_name[rs1]);
                f_reg[rd] = (double)((int)reg[rs1]);
            } else {
                printf("not set : %x\n", ins);
                exit(0);
            }
            break;
        case 0x08:
            sprintf(ins_str, "fmul.s\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            f_reg[rd] = f_reg[rs1] * f_reg[rs2];
            break;
        case 0xc:
            sprintf(ins_str, "fdiv.s\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
            f_reg[rd] = f_reg[rs1] / f_reg[rs2];
            break;
        case 0x11:
            if (funct3 == 0) {
                sprintf(ins_str, "fsgnj.d\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                if (f_reg[rs1]*f_reg[rs2]>0) {
                    f_reg[rd] = f_reg[rs1];
                } else {
                    f_reg[rd] = -f_reg[rs1];
                }
            } else {
                printf("not: %x\n", ins);
            }
            break;
        case 0x20:
            if (rs2 == 1) {
                sprintf(ins_str, "fcvt.s.d\t%s,%s", reg_name[rd], reg_name[rs1]);
                f_reg[rd] = f_reg[rs1];
            } else {
                printf("not: %x\n", ins);
            }
            break;
        case 0x21:
            if (rs2 == 0) {
                sprintf(ins_str, "fcvt.d.s\t%s,%s", reg_name[rd], reg_name[rs1]);
                f_reg[rd] = f_reg[rs1];
            } else {
                printf("not: %x\n", ins);
            }
            break;
        case 0x51:
            if (funct3 == 2) {
                sprintf(ins_str, "feq.d\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                if (f_reg[rs1] != f_reg[rs1] || f_reg[rs2] != f_reg[rs2]) {
                    reg[rd] = 0;
                } else {
                    if (f_reg[rs1] != f_reg[rs2]) {
                        reg[rd] = 0;
                    } else {
                        reg[rd] = 1;
                    }
                }
            } else if (funct3 == 1) {
                sprintf(ins_str, "flt.d\t%s,%s,%s", reg_name[rd], reg_name[rs1], reg_name[rs2]);
                if (f_reg[rs1] != f_reg[rs1] || f_reg[rs2] != f_reg[rs2]) {
                    reg[rd] = 0;
                } else {
                    if (f_reg[rs1] < f_reg[rs2]) {
                        reg[rd] = 1;
                    } else {
                        reg[rd] = 0;
                    }
                }
            } else {
                printf("not: %x\n", ins);
            }
            break;
        case 0x61:
            if (rs2 == 0) {
                sprintf(ins_str, "fcvt.w.d\t%s,%s", reg_name[rd], reg_name[rs1]);
                reg[rd] = (int)f_reg[rs1];
            } else {
                printf("not set: %x\n", ins);
                exit(0);
            }
            break;
        case 0x71:
            if (rs2 == 0 && funct3 == 0) {
                sprintf(ins_str, "fmv.x.d\t%s,%s", reg_name[rd], reg_name[rs1]);
                reg[rd] = FD2LL(f_reg[rs1]);
            } else {
                printf("not set: %x\n", ins);
                exit(0);
            }
            break;
        case 0x79:
            if (rs2 == 0 && funct3 == 0) {
                sprintf(ins_str, "fmw.d.x\t%s,%s", reg_name[rd], reg_name[rs1]);
                f_reg[rd] = LL2FD(reg[rs1]);
            } else {
                printf("not set: %x\n", ins);
                exit(0);
            }
            break;
        default:
            printf("not set: %x\n", ins);
            exit(0);
            break;
        }
        break;
    // default
    default:
        printf("not known ins : %x\n", ins);
        exit(0);
        break;
    }
    
    
}

void execute() {
    switch (alu_op) {
    case ADD:
        alu_res = alu_op_1 + alu_op_2;
        break;
    case ADDW:
        alu_res = (int)alu_op_1 + (int)alu_op_2;
        break;
    case SUB:
        alu_res = alu_op_1 - alu_op_2;
        break;
    case SLT:
        alu_res = (alu_op_1 < alu_op_2) ? 1 : 0;
        break;
    case SLTU:
        alu_res = ((ull)alu_op_1 < (ull)alu_op_2) ? 1 : 0;
        break;
    case XOR:
        alu_res = alu_op_1 ^ alu_op_2;
        break;
    case OR:
        alu_res = alu_op_1 | alu_op_2;
        break;
    case AND:
        alu_res = alu_op_1 & alu_op_2;
        break;
    case SLL:
        alu_res = alu_op_1 << alu_op_2;
        break;
    case SLLW:
        alu_res = (int)alu_op_1 << (int)alu_op_2;
        break;
    case SRL:  
        alu_res = (ull)alu_op_1 >> alu_op_2;
        break;
    case SRLW:  
        alu_res = (int)((uint)alu_op_1 >> (int)alu_op_2);
        break;
    case SRA:
        alu_res = alu_op_1 >> alu_op_2;
        break;
    case SRAW:
        alu_res = (int)alu_op_1 >> (int)alu_op_2;
        break;
    case MUL:
        alu_res = alu_op_1 * alu_op_2;
        break;
    case MULH:
        alu_res = ((__int128_t)alu_op_1 * (__int128_t)alu_op_2)>>64;
        break;
    case MULHSU:
        alu_res = ((__int128_t)alu_op_1 * (__uint128_t)(ull)alu_op_2)>>64;
        break;
    case MULHU:
        alu_res = ((__uint128_t)(ull)alu_op_1 * (__uint128_t)(ull)alu_op_2)>>64;
        break;
    case DIV:
        alu_res = alu_op_1 / alu_op_2;
        break;
    case DIVU:
        alu_res = (ull)alu_op_1 / (ull)alu_op_2;
        break;
    case REM:
        alu_res = alu_op_1 % alu_op_2;
        break;
    case REMU:
        alu_res = (ull)alu_op_1 % (ull)alu_op_2;
        break;
    case REMW:
        alu_res = (int)alu_op_1 % (int)alu_op_2;
        break;
    case REMUW:
        alu_res = (int)((uint)alu_op_1 % (uint)alu_op_2);
        break;
    case DIVW:
        alu_res = (int)alu_op_1 / (int)alu_op_2;
        break;
    case DIVUW:
        alu_res = (int)((uint)alu_op_1 / (uint)alu_op_2);
        break;
    case SUBW:
        alu_res = (int)alu_op_1 - (int)alu_op_2;
        break;
    case MULW:
        alu_res = (int)alu_op_1 * (int)alu_op_2;
        break;
    default:
        printf("dlu not implemented\n");
        break;
    }
}

void memoryOp() {

    void *dest = DEST(alu_res);
    if (mem_read) {
        switch (mem_type) {
            case MEM_B:
                reg[mem_reg] = *(char*)(dest);
                break;
            case MEM_BU:
                reg[mem_reg] = *(uchar*)(dest);
                break;
            case MEM_H:
                reg[mem_reg] = *(short*)(dest);
                break;
            case MEM_HU:
                reg[mem_reg] = *(ushort*)(dest);
                break;
            case MEM_W:
                reg[mem_reg] = *(int*)(dest);
                break;
            case MEM_WU:
                reg[mem_reg] = *(uint*)(dest);
                break;
            case MEM_D:
                reg[mem_reg] = *(ll*)(dest);
                break;
            case MEM_FD:
                f_reg[mem_reg] = LL2FD(*(ll*)(dest));
                break;
            case MEM_FW:
                f_reg[mem_reg] = W2FW(*(int*)(dest));
                break;
            default:
                break;
        }
    } else if (mem_write) {
        switch (mem_type) {
            case MEM_B:
            case MEM_BU:
                *(char*)(dest) = (char)reg[mem_reg]; 
                break;
            case MEM_H:
            case MEM_HU:
                *(short*)(dest) = (short)reg[mem_reg];
                break;
            case MEM_W:
            case MEM_WU:
                *(int*)(dest) = (int)reg[mem_reg];
                break;
            case MEM_D:
                *(ll*)(dest) = reg[mem_reg];
                break;
            case MEM_FD:
                *(ll*)(dest) = FD2LL(f_reg[mem_reg]);
                break;
            case MEM_FW:
                *(int*)(dest) = FW2W(f_reg[mem_reg]);
                break;
            default:
                break;
        }
    }
}

void writeback() {
    if (reg_write) {
        if (reg_dest_rd) {
            if (rd != 0) {
                reg[rd] = alu_res;
            }
        }
    }
}

static inline void change_pc() {
    pc = pc_branch ? pc_dest&(~1) : pc + 4;
}

void debug_command() {
    if (debug_mode) {
        do {
            if (debug_type == NONE) {
                while (debug());
            }
            if (debug_type == RUN) {
                if (debug_run_num != -1) {
                    debug_run_num--;
                    if (debug_run_num == -1) {
                        debug_type = NONE;
                        while (debug());
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else if (debug_type == PC) {
                if (pc == debug_pc_address) {
                    debug_type = NONE;
                    while (debug());
                } else {
                    break;
                }
            } else if (debug_type == REG) {
                if (reg[debug_reg] == debug_reg_val) {
                    debug_type = NONE;
                    while (debug());
                } else {
                    break;
                }
            } else if (debug_type == MEM) {
                if (*(ll*)DEST(debug_mem) == debug_mem_dat) {
                    debug_type = NONE;
                    while (debug());
                } else {
                    break;
                }
            } else if (debug_type == REG_W) {
                if (reg[debug_reg] != debug_reg_val) {
                    debug_type = NONE;
                    while (debug());
                } else {
                    break;
                }
            } else if (debug_type == MEM_WH) {
                if (*(ll*)DEST(debug_mem) != debug_mem_dat) {
                    debug_type = NONE;
                    while (debug());
                } else {
                    break;
                }
            }
        } while (debug_type != NONE);
    }
}

bool debug() {
    char str[100];
    printf("(debug) ");
    fgets(str, 90, stdin);
    char *tokens = strtok(str," \n");
    // 0: ins, 1: reg, 2: run, 3: mem, 4: until, 5: until_pc, 
    // 6: until_reg, 7: until_reg_num, 8: until_mem, 9: until_mem_addr, 10: done
    // 11: while, 12: while_reg, 13: while_reg_num, 14: while_mem, 15: while_mem_addr
    int mode = -1;
    while (tokens != NULL) {
        if (strcmp(tokens, "ins") == 0) {
            if (mode == -1) {
                mode = 0;
                printf("%llx: %s\n", pc, ins_str);
            } else {
                printf("wrong command\n");
            }
            return true;
        } else if (strcmp(tokens, "pc") == 0) {
            if (mode == -1) {
                printf("pc: %llx\n", pc);
                return true;
            } else if (mode == 4) {
                mode = 5;
            } else {
                printf("wrong command\n");
                return true;
            }
        } else if (strcmp(tokens, "reg") == 0) {
            if (mode == -1) {
                mode = 1;
            } else if (mode == 4) {
                mode = 6;
            } else if (mode == 11) {
                mode = 12;
            }
            else {
                printf("wrong command\n");
                return true;
            }
        } else if (isdigits(tokens)) {
            int num = atoi(tokens);
            if (mode == 1) {
                if (num < 0 || num > 31) {
                    printf("wrong command\n");
                    return true;
                } else {
                    printf("%s: %016llx\n", reg_name[num], reg[num]);
                    return true;
                }
            } else if (mode == 2) {
                if (num <= 0) {
                    printf("invalid number\n");
                    return true;
                } else {
                    debug_run_num = num;
                    debug_type = RUN;
                    return false;
                }
            } else if (mode == 6) {
                mode = 7;
                debug_reg = num;
            } else if (mode == 12) {
                mode = 13;
                debug_reg = num;
            }
            else {
                printf("invalid number\n");
                return true;
            }
        } else if (strcmp(tokens, "h") == 0) {
            if (mode != -1) {
                printf("wrong command\n");
            } else {
                printf("Interactive commands:\n");
                printf("reg [reg]\t\t\t# Display [reg] (all if omitted)\n");
                printf("h\t\t\t\t# This screen\n");
                printf("pc\t\t\t\t# Show corrent PC\n");
                printf("ins\t\t\t\t# Display current instruction\n");
                printf("q\t\t\t\t# End the simulation\n");
                printf("r [count]\t\t\t# Run [count] instructions (till the end if omitted)\n");
                printf("mem <hex addr>\t\t\t# Show contents of physical memory\n");
                printf("until pc <hex val>\t\t# Stop when PC hits <hex val>\n");
                printf("until mem <hex addr> <hex val>\t# Stop when memory <hex addr> becomes <hex val>\n");
                printf("until reg <reg> <hex val>\t# Stop when <reg> hits <hex val>\n");
                printf("while reg <reg> <hex val>\t# Run while <reg> is <val>\n");
                printf("while mem <hex addr> <hex val>\t# Run while memory <hex addr> is <hex val>\n");
            }
            return true;
        } else if (strcmp(tokens, "q") == 0) {
            if (mode != -1) {
                printf("wrong command\n");
                return true;
            } else {
                exit(0);
            }
        } else if (strcmp(tokens, "r") == 0) {
            if (mode != -1) {
                printf("wrong command\n");
                return true;
            } else {
                mode = 2;
            }
        } else if (strlen(tokens) > 2 && tokens[0] == '0' && tokens[1] == 'x') {
            char *end;
            ll num = strtol(tokens+2, &end, 16);
            if (*end) {
                printf("wrong address format\n");
                return true;
            } else {
                if (mode == 3) {
                    printf("0x%llx: 0x%llx\n", num, *(ll*)DEST(num));
                    return true;
                } else if (mode == 5) {
                    debug_pc_address = num;
                    debug_type = PC;
                    return false;
                } else if (mode == 7) {
                    debug_reg_val = num;
                    debug_type = REG;
                    return false;
                } else if (mode == 8) {
                    debug_mem = num;
                    mode = 9;
                } else if (mode == 9) {
                    debug_mem_dat = num;
                    debug_type = MEM;
                    return false;
                } else if (mode == 13) {
                    debug_reg_val = num;
                    debug_type = REG_W;
                    return false;
                } else if (mode == 14) {
                    mode = 15;
                    debug_mem = num;
                } else if (mode == 15) {
                    debug_mem_dat = num;
                    debug_type = MEM_WH;
                    return false;
                }
                else {
                    printf("wrong command\n");
                    return true;
                } 
            }
        } else if (strcmp(tokens, "mem") == 0) {
            if (mode == -1) {
                mode = 3;
            } else if (mode == 4) {
                mode = 8;
            } else if (mode == 11) {
                mode = 14;
            }
            else {
                printf("wrong command\n");
                return true;
            }
        } else if (strcmp(tokens, "until") == 0) {
            if (mode == -1) {
                mode = 4;
            } else {
                printf("wrong command\n");
                return true;
            }
        } else if (strcmp(tokens, "while") == 0) {
            if (mode == -1) {
                mode = 11;
            } else {
                printf("wrong command\n");
                return true;
            }
        }
        else {
            printf("wrong command\n");
            return true;
        }
        tokens = strtok(NULL, " \n");
    }
    if (mode == 1) {
        for (int i = 0; i < 32; ++i) {
            printf("%s: %016llx", reg_name[i], reg[i]);
            if (i%4==3) {
                printf("\n");
            } else {
                printf("\t");
            }
        }
        return true;
    } else if (mode == -1) {
        return true;
    } else if (mode == 2) {  // if is run forever
        debug_type = RUN;
        debug_run_num = -1;
        return false;
    }
    printf("wrong commad\n");
    return true;
}
