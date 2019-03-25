
static char OP_NAMES[][4] =
{
    "NOP",
    "ADX", "ADY", "SUX", "SUY", "LDA", "STA", "ADD", "SUB",
    "INA", "INX", "INY", "DEA", "DEX", "DEY", "PUA", "PPA",
    "CMP", "BIE", "BIN", "BIP", "JMP", "CAL", "RET", "XOR",
    "INT", "LDA", "LDX", "LDY", "LDA", "LDA", "TXA", "TYA",
    "AND", "INV", "SAL", "SAR", "ROL", "ROR", "TAX", "TAY",
    "TXY", "TYX", "CMX", "CMY", "BNE", "AOR",
};

enum
{
    OP_MODE_UNRESOLVED_VAL = -2,
    OP_MODE_UNRESOLVED_ADD = -1,
    OP_MODE_NONE       = 0,
    OP_MODE_VAL        = 1,
    OP_MODE_ADD        = 2,
    OP_MODE_REL_ADD    = 3
};

static char OP_MODES[] =
{
    0,
    0, 0, 0, 0, 1, 2, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 0, 1,
    1, 2, 1, 1, 3, 3, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 2, 1
};

static char OP_ARGS[] =
{
    0,
    0, 0, 0, 0, 1, 2, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 0, 1,
    1, 2, 1, 1, 2, 2, 0, 0,
    1, 0, 1, 1, 0, 0, 0, 0,
    0, 0, 1, 1, 2, 1
};

static char OP_CYCLES[] =
{
    2,
    3, 3, 3, 3, 3, 3, 4, 4,
    2, 2, 2, 2, 2, 2, 3, 3,
    4, 2, 2, 2, 2, 3, 3, 3,
    2, 3, 3, 3, 4, 4, 2, 2,
    3, 3, 3, 3, 3, 3, 2, 2,
    2, 2, 4, 4, 2, 3
};

/*
struct OP
{
u8          opcode   = 0;
u16         argument = 0;
char        op_mode  = OP_MODE_NONE;
std::string arg_id   = "";
};
*/

enum OP_CODES
{
    OP_NOP = 0x00, //no operation
    
    OP_ADX = 0x01,        //A = A + X
    OP_ADY = 0x02,        //A = A + Y
    OP_SUX = 0x03,        //A = A - X
    OP_SUY = 0x04,        //A = A - Y
    OPIV_LDA = 0x05,      //load value to A    (arg: 8-bit intermediate value)
    OPIA_STA = 0x06,      //store A to RAM     (arg: 16-bit intermediate address)
    OPIV_ADD = 0x07,      //add value to A     (arg: 8-bit  intermediate value)
    OPIV_SUB = 0x08,      //sub value from A   (arg: 8-bit  intermediate value)
    
    OP_INA = 0x09,        //A++
    OP_INX = 0x0A,        //X++
    OP_INY = 0x0B,        //Y++
    OP_DEA = 0x0C,        //A--
    OP_DEX = 0x0D,        //X--
    OP_DEY = 0x0E,        //Y--
    OP_PUA = 0x0F,        //push A on stack
    OP_PPA = 0x10,        //pop A from stack
    
    OP_CMP = 0x11,        //subtracts value from A and sets flags (arg: 8-bit intermediate value)
    OP_BIE = 0x12,        //branch if equal (if zero flag is set) (arg: 16-bit intermediate address)
    OP_BIN = 0x13,        //branch if negative (if underflow flag is set) (arg: 16-bit intermediate address)
    OP_BIP = 0x14,        //branch if positive (if underflow flag is unset) (arg: 16-bit intermediate address)
    OP_JMP = 0x15,        //jump to address (arg: 16-bit intermediate address)
    OP_CAL = 0x16,        //call a subroutine (arg: 16-bit intermedate address)
    OP_RET = 0x17,        //return from subroutine
    OP_XOR = 0x18,        //A = A ^ argument (arg: 8-bit intermediate value)
    
    OP_INT = 0x19,        //interrupt (arg: 8-bit intermediate value)
    OPIA_LDA = 0x1A,      //load value to A (arg: 16-bit intermediate address)
    OPIV_LDX = 0x1B,      //load value to X (arg: 8-bit intermediate value)
    OPIV_LDY = 0x1C,      //load value to Y (arg: 8-bit intermediate value)
    OPRAX_LDA = 0x1D,     //load value to A (arg: 16-bit relative address to X)
    OPRAY_LDA = 0x1E,     //load value to A (arg: 16-bit relative address to Y)
    OP_TXA = 0x1F,        //A = X
    OP_TYA = 0x20,        //A = Y
    
    OP_AND = 0x21,        //A = A & argument (arg: 8-bit intermediate value)
    OP_INV = 0x22,        //A = ~A
    OP_SAL = 0x23,        //A = A << argument (arg: 8-bit intermediate value)
    OP_SAR = 0x24,        //A = A >> argument (arg: 8-bit intermediate value)
    OP_ROR = 0x25,        //rotate A 1 bit right
    OP_ROL = 0x26,        //rotate A 1 bit left
    OP_TAX = 0x27,        //X = A
    OP_TAY = 0x28,        //Y = A
    
    OP_TXY = 0x29,        //Y = X
    OP_TYX = 0x2A,        //X = Y
    OP_CMX = 0x2B,        //subtracts value from X and sets flags (arg: 8-bit intermediate value)
    OP_CMY = 0x2C,        //subtracts value from Y and sets flags (arg: 8-bit intermediate value)
    OP_BNE = 0x2D,        //branch if not equal (if zero flag is unset) (arg: 16-bit intermediate address)
    OP_AOR = 0x2E,        //A = A | argument (arg: 8-bin intermediate value)
};
