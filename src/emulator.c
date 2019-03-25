#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <SDL2/SDL.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

#define strequ(x, y) !strcmp(x, y)

#define SET_BIT(raw, flag)   (raw |=   1 << (flag))
#define RESET_BIT(raw, flag) (raw &= ~(1 << (flag)))
#define GET_BIT(raw, flag)   ((1 << (flag)) & raw)

#define RAM_START     0x0000 //0x0800 bytes
#define RAM_SIZE      0x0800

#define STACK_START   0x0900 //0x0100 bytes, grows downwards
#define GPU_CTRL      0x0901 //0x0001 byte
//
#define GPU_VBLANK    0x0902 //0x0001 byte
#define CONTROLLER0   0x0903 //0x0002 bytes %abxyulrd %00(L2)(L1)(R2)(R1)(select)(start)
#define CONTROLLER1   0x0905 //0x0002 bytes %abxyulrd %00(L2)(L1)(R2)(R1)(select)(start)
#define PALETTE_ST    0x0907 //0x0001 byte
#define PALETTE_DT    0x0908 //0x0001 byte
#define SPRTEX_P      0x0909 //0x0002 bytes
#define BKGTEX_P      0x090B //0x0002 bytes
#define BKG_PAL_MAP   0x2E98 //0x0168 bytes
#define BKG_TEX_MAP   0x3000 //0x03C0 bytes
#define BKG_PALETTE   0x33C0 //0x0020 bytes
#define SPR_PALETTE   0x33E0 //0x0020 bytes
#define SCROLL_X      0x3400 //0x0001 byte
#define SCROLL_Y      0x3401 //0x0001 byte

#define ROM_START     0x7FFF //0x8000 bytes
#define ROM_PAGE_SIZE 0x8000

#define NUM_TILES     (32 * 30)
#define NUM_SPRITES   64
#define SCR_WIDTH     256
#define SCR_HEIGHT    240

#include "ops.h"

/*****************/
//ENUMERATORS
/*****************/

enum CPU_FLAGS
{
    CPU_ZERO      = 0,
    CPU_OVERFLOW  = 1,
    CPU_UNDERFLOW = 2,
    
    CPU_TERMINATE = 7
};

enum MEM_ACCESS_MODE
{
    READ  = 0,
    WRITE = 1
};

enum CONTROLLER_KEYS_0
{
    KEY_DOWN = 0,
    KEY_RIGHT,
    KEY_LEFT,
    KEY_UP,
    KEY_Y,
    KEY_X,
    KEY_B,
    KEY_A
};
enum CONTROLLER_KEYS_1
{
    KEY_START = 0,
    KEY_SELECT,
    KEY_R1,
    KEY_R2,
    KEY_L1,
    KEY_L2
};

static u32 VGA_PALLETTE[64] =
{
    0x464646ff,  0x00065aff,  0x000678ff,  0x020673ff,
    0x35034cff,  0x57000eff,  0x5a0000ff,  0x410000ff,
    0x120200ff,  0x001400ff,  0x001e00ff,  0x001e00ff,
    0x001521ff,  0x000000ff,  0x000000ff,  0x000000ff,
    0x9d9d9dff,  0x004ab9ff,  0x0530e1ff,  0x5718daff,
    0x9f07a7ff,  0xcc0255ff,  0xcf0b00ff,  0xa42300ff,
    0x5c3f00ff,  0x0b5800ff,  0x006600ff,  0x006713ff,
    0x005e6eff,  0x000000ff,  0x000000ff,  0x000000ff,
    0xfeffffff,  0x1f9effff,  0x5376ffff,  0x9865ffff,
    0xfc67ffff,  0xff6cb3ff,  0xff7466ff,  0xff8014ff,
    0xc49a00ff,  0x71b300ff,  0x28c421ff,  0x00c874ff,
    0x00bfd0ff,  0x2b2b2bff,  0x000000ff,  0x000000ff,
    0xfeffffff,  0x9ed5ffff,  0xafc0ffff,  0xd0b8ffff,
    0xfebfffff,  0xffc0e0ff,  0xffc3bdff,  0xffca9cff,
    0xe7d58bff,  0xc5df8eff,  0xa6e6a3ff,  0x94e8c5ff,
    0x92e4ebff,  0xa7a7a7ff,  0x000000ff,  0x000000ff
};

//prototypes
void put_pix   (u32 x, u32 y, u32 index);
u8   mem_access(u8 mode, u16 address, u8 value);
#define WB(address, value) mem_access(WRITE, address, value)
#define RB(address)        mem_access(READ,  address, 0)

//RAM
static u8     RAM[0x10000];
static u8     cart_page     = 0;
static u8     cart_page_max = 0;
static u8*    cart_buffer   = NULL;

/*****************/
//GPU
/*****************/

#define SPRITE_WIDTH  8
#define SPRITE_HEIGHT 8
typedef struct
{
    u8 x, y, ctrl, tex_id;
    /*ctrl - 00nnnvha
     * nnn = palette
     * v   = 1 - vertical flip
     * h   = 1 - horizontal flip
     * a   = 1 - sprite invisible
     */
} sprite_t;

typedef struct
{
    u16 sdata;  //sprite data pointer, 64 sprites
    
    u8  vblank;
    
    u8  write_reg_high;
    
    u16 palette_index;
    u16 sprtex_p;
    u16 bkgtex_p;
    
    u8  scroll_x;
    u8  scroll_y;
    
    u8  ctrl;
    /* fff00nnn
     * fff     = define sprite data 0x0(fff)00
     * nnn     = define default bg color
               - 0 = black
               - 1 = white
               - 2 = red
               - 3 = green
               - 4 = blue
               - 5 = light blue
               - 6 = yellow
               - 7 = magenta
     */
    
    u8 default_background_color;
    
    u32 tick_index;
} gpu_t; static gpu_t gpu;

//bit field operations
static inline u8 bit(u8* array, u32 bit_index)
{
    return !!GET_BIT(array[bit_index / 8], 7 - (bit_index % 8));
}

static inline u8 get_triplet(u8* array, u32 bit_index)
{
    return (bit(array, bit_index + 0) << 2) |
           (bit(array, bit_index + 1) << 1) |
           (bit(array, bit_index + 2) << 0);
}

//process 1 pixel
void gpu_exec()
{
    gpu.tick_index = (gpu.tick_index + 1) % ((SCR_WIDTH * SCR_HEIGHT) + ((SCR_WIDTH * SCR_HEIGHT) / 3));
    gpu.vblank     =  gpu.tick_index >= (SCR_WIDTH * SCR_HEIGHT);
    
    //if vblank is not active
    //start drawing
    if(!gpu.vblank)
    {
        //pixel coordinates
        u8 pix_x     = gpu.tick_index % SCR_WIDTH;
        u8 pix_y     = gpu.tick_index / SCR_WIDTH;
        
    /* background processing */
        
        //background scroll
        u8 scrolled_pix_x = pix_x - RAM[SCROLL_X];
        u8 scrolled_pix_y = pix_y - RAM[SCROLL_Y];
        
        //get background tile index coordinates
        u8 bg_x = scrolled_pix_x / 8;
        u8 bg_y = scrolled_pix_y / 8;
        
        //get pixel and tile offset
        u8 x_off = scrolled_pix_x - bg_x * 8;
        u8 y_off = scrolled_pix_y - bg_y * 8;
        
        //get tile index in background texture map
        u8 bg_tex_map_index = RAM[BKG_TEX_MAP + bg_x + bg_y * 32];
        //get tile color palette in 3 bit array background palette map
        //TODO: check if this is right
        u8 bg_pal_map_index = get_triplet(RAM + BKG_PAL_MAP, (bg_x + bg_y * 32) * 3) * 4;
        
        u16 tex_offset;
        u8  pix_color;
        
        //if tile is not zero, draw tile
        if(bg_tex_map_index != 0)
        {
            tex_offset = gpu.bkgtex_p + bg_tex_map_index * 16;
            pix_color = (RAM[tex_offset + y_off + 0] >> (7 - x_off) & 1) +
                        (RAM[tex_offset + y_off + 8] >> (7 - x_off) & 1) * 2;
            
            put_pix(pix_x, pix_y, RAM[BKG_PALETTE + pix_color + bg_pal_map_index]);
        }
        //else draw default background color
        else
        {
            //draw background color
            put_pix(pix_x, pix_y, gpu.default_background_color);
        }
        
    /* sprite processing */
        for(u32 i = 0; i < NUM_SPRITES; i++)
        {
            u8 sp_x      = RAM[(gpu.sdata + i * 4 + 0) % RAM_SIZE];
            u8 sp_y      = RAM[(gpu.sdata + i * 4 + 1) % RAM_SIZE];
            u8 sp_ctrl   = RAM[(gpu.sdata + i * 4 + 2) % RAM_SIZE];
            u8 sp_tex_id = RAM[(gpu.sdata + i * 4 + 3) % RAM_SIZE];
            
            if(sp_ctrl & 1                 && //if sprite visible
               
               pix_x >= sp_x               && //if
               pix_x < sp_x + SPRITE_WIDTH && //pixel coordinates
               pix_y >= sp_y               && //are within
               pix_y < sp_y + SPRITE_HEIGHT)  //sprite bounding box
            {
                //calculate pixel offset
                x_off = sp_ctrl & 2 ? SPRITE_WIDTH  - (pix_x - sp_x) - 1 : pix_x - sp_x;
                y_off = sp_ctrl & 4 ? SPRITE_HEIGHT - (pix_y - sp_y) - 1 : pix_y - sp_y;
                //get texture offset
                tex_offset = (gpu.sprtex_p + sp_tex_id * 16);
                
                //get pixel color from texture
                pix_color = (RAM[tex_offset + y_off + 0] >> (7 - x_off) & 1) +
                            (RAM[tex_offset + y_off + 8] >> (7 - x_off) & 1) * 2;
                
                //if not zero, draw the pixel with sprite palette
                if(pix_color != 0)
                {
                    put_pix(pix_x, pix_y, RB(SPR_PALETTE + pix_color + ((sp_ctrl >> 3) & 0x7) * 4));
                }
            }
        }
    }
}

/*****************/
//CPU + RAM ACCESS
/*****************/

//big endian CPU
typedef struct
{
    //registers
    u8 A, X, Y, SP;
    u8 flags;

    u16 PC;
} cpu_t; static cpu_t cpu;


//ram access
u8 mem_access(u8 mode, u16 address, u8 value)
{
    //ram + palettes + map access
    if((address >= RAM_START   && address < RAM_SIZE)    ||
       (address >= BKG_PAL_MAP && address < BKG_TEX_MAP) ||
       (address >= BKG_TEX_MAP && address < SCROLL_X)    ||
       (address >= SCROLL_X    && address <= SCROLL_Y))
    {
        if(mode) { RAM[address] = value; return 0; } else { return RAM[address]; };
    }
    //gpu control
    else if(address == GPU_CTRL)
    {
        if(mode)
        {
            gpu.ctrl  = value;
            //if(gpu.ctrl & 1) { gpu.sram = 0x2000; gpu.bram = 0x1000; }
            //else             { gpu.sram = 0x1000; gpu.bram = 0x2000; }
            switch(gpu.ctrl & 7)
            {
                case 0: { gpu.default_background_color = 13; break; } //black
                case 1: { gpu.default_background_color = 46; break; } //white
                case 2: { gpu.default_background_color = 21; break; } //red
                case 3: { gpu.default_background_color = 41; break; } //green
                case 4: { gpu.default_background_color = 33; break; } //blue
                case 5: { gpu.default_background_color = 47; break; } //light blue
                case 6: { gpu.default_background_color = 53; break; } //yellow
                case 7: { gpu.default_background_color = 34; break; } //magenta
            }
            gpu.sdata = (gpu.ctrl << 3) & 0x0700;
            return 0;
        } else { return gpu.ctrl; }
    }
    //gpu vblank
    else if(address == GPU_VBLANK)
    {
        //writes turn to reads
        return gpu.vblank;
    }
    //controllers
    else if(address >= CONTROLLER0 && address <= CONTROLLER1 + 1)
    {
        //writes turn to reads
        return RAM[address];
    }
    else if (address >= ROM_START && address <= 0xFFFF)
    {
        if(cart_page == 0)
        {
            return RAM[address];
        }
        else
        {
            return cart_buffer[address + ROM_PAGE_SIZE * (cart_page - 1)];
        }
    }
    //palette start index
    else if(address == PALETTE_ST)
    {
        //reads turn to writes
        gpu.palette_index  =  gpu.write_reg_high ? (gpu.palette_index & 0x00FF) | (value << 8) : (gpu.palette_index & 0xFF00) | (value & 0x00FF);
        gpu.write_reg_high = !gpu.write_reg_high;
    }
    //palette data
    else if(address == PALETTE_DT)
    {
        //reads turn to writes
        return WB(gpu.palette_index++, value);
    }
    //texture data 0
    else if(address == SPRTEX_P)
    {
        //reads turn to writes
        gpu.sprtex_p       =  gpu.write_reg_high ? (gpu.sprtex_p & 0x00FF) | (value << 8) : (gpu.sprtex_p & 0xFF00) | (value & 0x00FF);
        gpu.write_reg_high = !gpu.write_reg_high;
    }
    //texture data 1
    else if(address == BKGTEX_P)
    {
        //reads turn to writes
        gpu.bkgtex_p       =  gpu.write_reg_high ? (gpu.bkgtex_p & 0x00FF) | (value << 8) : (gpu.bkgtex_p & 0xFF00) | (value & 0x00FF);
        gpu.write_reg_high = !gpu.write_reg_high;
    }
    
    return 0;
    
    /*switch(address)
    {
        //ram access
        case RAM_START ... 0x07FF:
        {
            if(mode) { RAM[address] = value; return 0; } else { return RAM[address]; } break;
        }
        //gpu control
        case GPU_CTRL:
        {
            if(mode)
            {
                gpu.ctrl  = value;
                //if(gpu.ctrl & 1) { gpu.sram = 0x2000; gpu.bram = 0x1000; }
                //else             { gpu.sram = 0x1000; gpu.bram = 0x2000; }
                gpu.sdata = (gpu.ctrl << 3) & 0x0700;
                return 0;
            } else { return gpu.ctrl; }
            break;
        }
        //writes turns into reads
        case GPU_VBLANK: { return gpu.vblank; break; }
            
        //controllers
        //writes turns into reads
        case (CONTROLLER0 + 0): { return RAM[CONTROLLER0 + 0]; break; }
        case (CONTROLLER0 + 1): { return RAM[CONTROLLER0 + 1]; break; }
        case (CONTROLLER1 + 0): { return RAM[CONTROLLER1 + 0]; break; }
        case (CONTROLLER1 + 1): { return RAM[CONTROLLER1 + 1]; break; }
            
        //pass array data
        case PALETTE_ST:
        {
            //reads turn to writes
            gpu.palette_index  =  gpu.write_reg_high ? (gpu.palette_index & 0x00FF) | (value << 8) : (gpu.palette_index & 0xFF00) | (value & 0x00FF);
            gpu.write_reg_high = !gpu.write_reg_high;
            return 0;
            break;
        }
        case PALETTE_DT:
        {
            //reads turn to writes
            return WB(gpu.palette_index++, value);
            break;
        }
            
        //texture data
        case SPRTEX_P:
        {
            //reads turn to writes
            gpu.sprtex_p     = gpu.write_reg_high ? (gpu.sprtex_p & 0x00FF) | (value << 8) : (gpu.sprtex_p & 0xFF00) | (value & 0x00FF);
            gpu.write_reg_high = !gpu.write_reg_high;
            return 0;
            break;
        }
        case BKGTEX_P:
        {
            //reads turn to writes
            gpu.bkgtex_p     = gpu.write_reg_high ? (gpu.bkgtex_p & 0x00FF) | (value << 8) : (gpu.bkgtex_p & 0xFF00) | (value & 0x00FF);
            gpu.write_reg_high = !gpu.write_reg_high;
            return 0;
            break;
        }
        
        //palettes
        case BKG_MAP     ... 0x33BF:
        case BKG_PALETTE ... 0x33C3:
        case SPR_PALETTE ... 0x33C7:
        {
            if(mode) { RAM[address] = value; return 0; } else { return RAM[address]; }
            break;
        }
            
        //rom access
        case ROM_START ... 0xFFFF:
        {
            //writes turn into reads
            return RAM[address];
            break;
        }
            
            
        default:
        {
            return 0;
            break;
        }
    }*/
}

//stack operations
void   push(u8 value) { WB(STACK_START | cpu.SP--, value); }
u8     pop()          { return RB(STACK_START | ++cpu.SP); }

//execute one intruction
void cpu_exec()
{
    //fetch the operation code
    u8 op_code = RB(cpu.PC++);
    
#ifdef STEP
    printf("executing [0x%02x: %s]\npre: (A: %u) (X: %u) (Y: %u) (PC: %u) (SP: %u) "
           "(flags: %u%u%u%u%u%u%u%u)\n", op_code, OP_NAMES[op_code], cpu.A, cpu.X, cpu.Y, cpu.PC, cpu.SP, !!GET_BIT(cpu.flags, CPU_TERMINATE), 0, 0, 0, 0, !!GET_BIT(cpu.flags, CPU_UNDERFLOW), !!GET_BIT(cpu.flags, CPU_OVERFLOW), !!GET_BIT(cpu.flags, CPU_ZERO));
#endif
    
#define CHECK_OVERFLOW(x, y)  if((x) > 0xff - (y)) { SET_BIT(cpu.flags, CPU_OVERFLOW);  } else { RESET_BIT(cpu.flags, CPU_OVERFLOW);  }
#define CHECK_UNDERFLOW(x, y) if((x) < (y))        { SET_BIT(cpu.flags, CPU_UNDERFLOW); } else { RESET_BIT(cpu.flags, CPU_UNDERFLOW); }
#define CHECK_ZERO(x)         if((x) == 0)         { SET_BIT(cpu.flags, CPU_ZERO);      } else { RESET_BIT(cpu.flags, CPU_ZERO);      }
    
    //execute op code
    switch(op_code)
    {
        case OP_NOP:   { break; }
            
        case OP_ADX:   { CHECK_OVERFLOW(cpu.A, cpu.X); cpu.A += cpu.X; CHECK_ZERO(cpu.A); break; }
        case OP_ADY:   { CHECK_OVERFLOW(cpu.A, cpu.Y); cpu.A += cpu.Y; CHECK_ZERO(cpu.A); break; }
            
        case OP_SUX:   { CHECK_UNDERFLOW(cpu.A, cpu.X) cpu.A -= cpu.X; CHECK_ZERO(cpu.A); break; }
        case OP_SUY:   { CHECK_UNDERFLOW(cpu.A, cpu.Y) cpu.A -= cpu.Y; CHECK_ZERO(cpu.A); break; }
        
        case OPIV_LDA: { cpu.A = RB(cpu.PC++); CHECK_ZERO(cpu.A); break; }
        case OPIA_LDA:
        {
            cpu.A = RB((RB(cpu.PC++) << 8) | RB(cpu.PC++));
            CHECK_ZERO(cpu.A); break;
        }
        case OPRAX_LDA:
        {
            cpu.A = RB(((RB(cpu.PC++) << 8) | RB(cpu.PC++)) + cpu.X);
            CHECK_ZERO(cpu.A); break;
        }
            
        case OPRAY_LDA:
        {
            cpu.A = RB(((RB(cpu.PC++) << 8) | RB(cpu.PC++)) + cpu.Y);
            CHECK_ZERO(cpu.A); break;
        }
            
        case OPIA_STA:
        {
            WB((RB(cpu.PC++) << 8) | RB(cpu.PC++), cpu.A);
            break;
        }
            
        case OPIV_ADD: { u8 arg = RB(cpu.PC++); CHECK_OVERFLOW(cpu.A, arg);  cpu.A += arg; CHECK_ZERO(cpu.A); break; }
        case OPIV_SUB: { u8 arg = RB(cpu.PC++); CHECK_UNDERFLOW(cpu.A, arg); cpu.A -= arg; CHECK_ZERO(cpu.A); break; }
            
        case OP_INA: { CHECK_OVERFLOW(cpu.A, 1); cpu.A++; CHECK_ZERO(cpu.A); break; }
        case OP_INX: { CHECK_OVERFLOW(cpu.X, 1); cpu.X++; CHECK_ZERO(cpu.X); break; }
        case OP_INY: { CHECK_OVERFLOW(cpu.Y, 1); cpu.Y++; CHECK_ZERO(cpu.Y); break; }
            
        case OP_DEA: { CHECK_UNDERFLOW(cpu.A, 1); cpu.A--; CHECK_ZERO(cpu.A); break; }
        case OP_DEX: { CHECK_UNDERFLOW(cpu.X, 1); cpu.X--; CHECK_ZERO(cpu.X); break; }
        case OP_DEY: { CHECK_UNDERFLOW(cpu.Y, 1); cpu.Y--; CHECK_ZERO(cpu.Y); break; }
          
        case OP_PUA: { push(cpu.A);                      break; }
        case OP_PPA: { cpu.A = pop(); CHECK_ZERO(cpu.A); break; }
            
        case OP_CMP: { u8 arg = RB(cpu.PC++); CHECK_UNDERFLOW(cpu.A, arg); CHECK_ZERO(cpu.A - arg); break; }
            
        case OP_BIE: { if(GET_BIT(cpu.flags,       CPU_ZERO)) { cpu.PC = (RB(cpu.PC++) << 8) | RB(cpu.PC++); } else { cpu.PC += 2; } break; }
        case OP_BNE: { if(!GET_BIT(cpu.flags,      CPU_ZERO)) { cpu.PC = (RB(cpu.PC++) << 8) | RB(cpu.PC++); } else { cpu.PC += 2; } break; }
        case OP_BIN: { if(GET_BIT(cpu.flags,  CPU_UNDERFLOW)) { cpu.PC = (RB(cpu.PC++) << 8) | RB(cpu.PC++); } else { cpu.PC += 2; } break; }
        case OP_BIP: { if(!GET_BIT(cpu.flags, CPU_UNDERFLOW)) { cpu.PC = (RB(cpu.PC++) << 8) | RB(cpu.PC++); } else { cpu.PC += 2; } break; }
        case OP_JMP: { cpu.PC = (RB(cpu.PC++) << 8) | RB(cpu.PC++); break; }
            
        case OP_CAL:
        {
            u16 address = (RB(cpu.PC++) << 8) | RB(cpu.PC++);
            push(cpu.PC >> 8);
            push(cpu.PC);
            cpu.PC      = address;
            break;
        }
        case OP_RET: { cpu.PC = (pop() << 8) | (pop()); break; }
            
        case OP_XOR: { cpu.A ^= RB(cpu.PC++); CHECK_ZERO(cpu.A); break; }
            
        case OP_INT:
        {
            u8 arg = RB(cpu.PC++);
            
            switch(arg)
            {
                case 0x01: { SET_BIT(cpu.flags, CPU_TERMINATE); break; }
                case 0x10: { fputc(cpu.A, stdout);              break; } //TODO: replace with my gpu implementation
                default: { break; }
            }
            
            break;
        }
            
        case OPIV_LDX: { cpu.X = RB(cpu.PC++); CHECK_ZERO(cpu.X); break; }
            
        case OPIV_LDY: { cpu.Y = RB(cpu.PC++); CHECK_ZERO(cpu.Y); break; }
            
        case OP_TXA: { cpu.A = cpu.X; CHECK_ZERO(cpu.A); break; }
        case OP_TYA: { cpu.A = cpu.Y; CHECK_ZERO(cpu.A); break; }
        case OP_TAX: { cpu.X = cpu.A; CHECK_ZERO(cpu.X); break; }
        case OP_TYX: { cpu.X = cpu.Y; CHECK_ZERO(cpu.X); break; }
        case OP_TAY: { cpu.Y = cpu.A; CHECK_ZERO(cpu.Y); break; }
        case OP_TXY: { cpu.Y = cpu.X; CHECK_ZERO(cpu.Y); break; }
            
        case OP_AND: { cpu.A &= RB(cpu.PC++);  CHECK_ZERO(cpu.A); break; }
        case OP_INV: { cpu.A = ~cpu.A;         CHECK_ZERO(cpu.A); break; }
        case OP_SAL: { cpu.A <<= RB(cpu.PC++); CHECK_ZERO(cpu.A); break; }
        case OP_SAR: { cpu.A >>= RB(cpu.PC++); CHECK_ZERO(cpu.A); break; }
        case OP_AOR: { cpu.A |= RB(cpu.PC++);  CHECK_ZERO(cpu.A); break; }
            
        case OP_ROR: { cpu.A = (cpu.A << 7) | (cpu.A >> 1); CHECK_ZERO(cpu.A); break; }
        case OP_ROL: { cpu.A = (cpu.A >> 7) | (cpu.A << 1); CHECK_ZERO(cpu.A); break; }
            
        case OP_CMX: { u8 arg = RB(cpu.PC++); CHECK_UNDERFLOW(cpu.X, arg); CHECK_ZERO(cpu.X - arg); break; }
        case OP_CMY: { u8 arg = RB(cpu.PC++); CHECK_UNDERFLOW(cpu.Y, arg); CHECK_ZERO(cpu.Y - arg); break; }
            
        default: { break; }
    }
    
    //emulate op cycles
    //1 cpu cycles = 3 gpu cycles
    for(u8 i = 0; i < OP_CYCLES[op_code]; i++)
    {
        gpu_exec(); gpu_exec(); gpu_exec();
    }
    
#ifdef STEP
    printf("post: (A: %u) (X: %u) (Y: %u) (PC: %u) (SP: %u) "
           "(flags: %u%u%u%u%u%u%u%u)\n", cpu.A, cpu.X, cpu.Y, cpu.PC, cpu.SP, !!GET_BIT(cpu.flags, CPU_TERMINATE), 0, 0, 0, 0, !!GET_BIT(cpu.flags, CPU_UNDERFLOW), !!GET_BIT(cpu.flags, CPU_OVERFLOW), !!GET_BIT(cpu.flags, CPU_ZERO));
#endif
}

/*****************/
//EMULATOR
/*****************/

SDL_Window*   window;
SDL_Renderer* renderer;
SDL_Texture*  texture;
u32*          pixels;

//init emulator
void emu_init()
{
    cpu.PC    = ROM_START;
    cpu.SP    = 0xff;
    cpu.flags = 0;
    
    
    gpu.ctrl       = 0;
    gpu.sdata      = 0x0300;
    gpu.tick_index = 0;
    gpu.vblank     = 0;
    
    gpu.default_background_color = 0x3F;
    
    gpu.palette_index  = 0;
    gpu.write_reg_high = 1; //big endian
    
    gpu.scroll_x = gpu.scroll_y = 0;
    
    SDL_Init(SDL_INIT_VIDEO);
    
    window   = SDL_CreateWindow("CPU", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCR_WIDTH, SCR_HEIGHT, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, SCR_WIDTH, SCR_HEIGHT);
    
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xff);
    SDL_RenderClear(renderer);
    
    pixels   = malloc(SCR_WIDTH * SCR_HEIGHT * 4);
}

//draw pixel on screen
static u32 start_ticks = 0;
static u32 end_ticks   = 0;
void put_pix(u32 x, u32 y, u32 index)
{
    pixels[y * SCR_WIDTH + x] = VGA_PALLETTE[index];
    
    if(x == SCR_WIDTH - 1 && y == SCR_HEIGHT - 1)
    {
        static SDL_Event e;
        
        while(SDL_PollEvent(&e) != 0)
        {
            switch(e.type)
            {
                case SDL_QUIT: { SET_BIT(cpu.flags, CPU_TERMINATE); break; }
                case SDL_KEYDOWN:
                {
                    switch(e.key.keysym.sym)
                    {
                        case SDLK_DOWN:  { SET_BIT(RAM[CONTROLLER0], KEY_DOWN);  break; }
                        case SDLK_RIGHT: { SET_BIT(RAM[CONTROLLER0], KEY_RIGHT); break; }
                        case SDLK_LEFT:  { SET_BIT(RAM[CONTROLLER0], KEY_LEFT);  break; }
                        case SDLK_UP:    { SET_BIT(RAM[CONTROLLER0], KEY_UP);    break; }
                        case SDLK_v:     { SET_BIT(RAM[CONTROLLER0], KEY_A);     break; }
                        case SDLK_c:     { SET_BIT(RAM[CONTROLLER0], KEY_B);     break; }
                        case SDLK_f:     { SET_BIT(RAM[CONTROLLER0], KEY_X);     break; }
                        case SDLK_d:     { SET_BIT(RAM[CONTROLLER0], KEY_Y);     break; }
                            
                        case SDLK_e:     { SET_BIT(RAM[CONTROLLER0 + 1], KEY_SELECT); break; }
                        case SDLK_r:     { SET_BIT(RAM[CONTROLLER0 + 1], KEY_START);  break; }
                        case SDLK_s:     { SET_BIT(RAM[CONTROLLER0 + 1], KEY_L1);     break; }
                        case SDLK_w:     { SET_BIT(RAM[CONTROLLER0 + 1], KEY_L2);     break; }
                        case SDLK_g:     { SET_BIT(RAM[CONTROLLER0 + 1], KEY_R1);     break; }
                        case SDLK_t:     { SET_BIT(RAM[CONTROLLER0 + 1], KEY_R2);     break; }
                        default: { break; }
                    }
                    break;
                }
                case SDL_KEYUP:
                {
                    switch(e.key.keysym.sym)
                    {
                        case SDLK_DOWN:  { RESET_BIT(RAM[CONTROLLER0], KEY_DOWN);  break; }
                        case SDLK_RIGHT: { RESET_BIT(RAM[CONTROLLER0], KEY_RIGHT); break; }
                        case SDLK_LEFT:  { RESET_BIT(RAM[CONTROLLER0], KEY_LEFT);  break; }
                        case SDLK_UP:    { RESET_BIT(RAM[CONTROLLER0], KEY_UP);    break; }
                        case SDLK_v:     { RESET_BIT(RAM[CONTROLLER0], KEY_A);     break; }
                        case SDLK_c:     { RESET_BIT(RAM[CONTROLLER0], KEY_B);     break; }
                        case SDLK_f:     { RESET_BIT(RAM[CONTROLLER0], KEY_X);     break; }
                        case SDLK_d:     { RESET_BIT(RAM[CONTROLLER0], KEY_Y);     break; }
                            
                        case SDLK_e:     { RESET_BIT(RAM[CONTROLLER0 + 1], KEY_SELECT); break; }
                        case SDLK_r:     { RESET_BIT(RAM[CONTROLLER0 + 1], KEY_START);  break; }
                        case SDLK_s:     { RESET_BIT(RAM[CONTROLLER0 + 1], KEY_L1);     break; }
                        case SDLK_w:     { RESET_BIT(RAM[CONTROLLER0 + 1], KEY_L2);     break; }
                        case SDLK_g:     { RESET_BIT(RAM[CONTROLLER0 + 1], KEY_R1);     break; }
                        case SDLK_t:     { RESET_BIT(RAM[CONTROLLER0 + 1], KEY_R2);     break; }
                        default: { break; }
                    }
                    break;
                }
                default:       { break; }
            }
        }
        
        SDL_UpdateTexture(texture, NULL, pixels, SCR_WIDTH * sizeof(u32));
        
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        
        //keep fps in certain range
        end_ticks = SDL_GetTicks() - start_ticks;
        
        if (1000.0f / 60.0 > end_ticks)
        {
            SDL_Delay(1000.0f / 60.0 - end_ticks);
        }
        
        //printf("fps: %g\n", 1000.0 / end_ticks);
        
        start_ticks = SDL_GetTicks();
    }
}

//load ROM into RAM
void emu_load(u8* program, u32 rom_size)
{
    if(rom_size > ROM_PAGE_SIZE)
    {
        //load first page into the ram
        //text section of the program must be in first page!
        for(u32 i = 0; i < ROM_PAGE_SIZE; i++) { RAM[(ROM_START + i)] = program[i]; }
        
        //init other pages
        cart_buffer = malloc(rom_size - ROM_PAGE_SIZE);
    }
    else
    {
        for(u32 i = 0; i < rom_size; i++) { RAM[(ROM_START + i)] = program[i]; }
    }
}

//main program
int main(int argc, char* argv[])
{
    //open file
    if(argc != 2)
    {
        printf("usage emu [rom.bin]\n"); return 1;
    }
    FILE* in = fopen(argv[1], "rb");
    
    /*
     * ROM LAYOUT *
     * header - 3 byte = { 'C', 'M', 'U' }
     *          1 byte = number of pages
     * data
     */
    //read file into buffer
    if(in == NULL) { printf("error opening file: %s\n", argv[1]); return 1; }
    
    //check header
    /*u8 header[3];
    
    fread(header, sizeof(char), 3, in);
    
    if(header[0] != 'C' || header[1] != 'M' || header[2] != 'U')
    {
        printf("file: \"%s\" is not CMU file!\n", argv[1]);
        fclose(in);
        return 0;
    }
    
    //get memory paging info
    fread(&cart_page_max, sizeof(char), 1, in);*/
    
    //load first page into the RAM
    fseek(in, 0, SEEK_END);
    u32 rom_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    u8* buffer = malloc(rom_size);
    
    fread(buffer, sizeof(char), rom_size, in);
    
    //init emulator
    emu_init();
    
    //load rom
    emu_load(buffer, rom_size);
    
    //emulator loop
    while(!GET_BIT(cpu.flags, CPU_TERMINATE))
    {
        cpu_exec();
#ifdef STEP
        //usleep(100000);
#endif
    }
    
    free(pixels);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_DestroyTexture(texture);
    SDL_Quit();
    
    return 0;
}
