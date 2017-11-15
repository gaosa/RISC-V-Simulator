#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdbool.h"
#include "unistd.h"
#include "ctype.h"
#include <sys/time.h>

typedef long long ll;
typedef unsigned long long ull;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;

static const char * reg_name[] = {
    "zero", "ra", "sp", "gp",
    "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1",
    "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3",
    "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11",
    "t3", "t4", "t5", "t6"
};

// memory
void *mem;
// stack
void *stack;
// register file : ll and double
ll reg[32];
double f_reg[32];
// PC
ll pc;
// instruction
uint ins;
char ins_str[100];
// decode res
int opcode, rd, rs1, rs2, rs3, funct3, funct7, funct2, i_imm, s_imm, sb_imm, u_imm, uj_imm;
// ALU options
enum OP_TYPE {ADD, ADDW, SUB, SLT, SLTU, 
    XOR, OR, AND, SLL, SLLW, SRL, SRLW, 
    SRA, SRAW, MUL, MULH, MULHSU, MULHU, 
    DIV, DIVU, REM, REMU, REMW, REMUW, 
    DIVW, DIVUW, SUBW, MULW} alu_op;
long long alu_op_1, alu_op_2, alu_res;
// reg options
bool reg_write;
bool reg_dest_rd;
// pc jump sig
bool pc_branch;
long long pc_dest;
// memory signal
bool mem_read, mem_write;
int mem_reg;
enum MEM_TYPE { MEM_B, MEM_H, MEM_W, MEM_D, MEM_BU, MEM_HU, MEM_WU, MEM_FD, MEM_FW } mem_type;

// debug mode
bool debug_mode;
char ins_str[100];
enum DEBUG_TYPE { NONE, RUN, PC, REG, REG_W, MEM, MEM_WH } debug_type;
int debug_run_num, debug_reg;
ll debug_pc_address, debug_reg_val, debug_mem, debug_mem_dat;

// helper functions
// for fetch instructions
#define GET(hi, lo) ((ins<<(31-hi))>>(31+lo-hi))
#define signExtImm12(imm) ((imm<<20)>>20)
#define signExtImm13(imm) ((imm<<19)>>19)
#define signExtImm21(imm) ((imm<<11)>>11)
#define THRES 0x5000000000000000LL
static inline ll FD2LL(double a);
static inline double LL2FD(ll a);
static inline int FW2W(float a);
static inline float W2FW(int a);
static inline void * DEST(ll offset);
static inline bool isdigits(char* str);

int setup(char *filename);
static inline void fetch();
void decode();
void execute();
void memoryOp();
void writeback();
static inline void change_pc();
bool debug();
void debug_command();
