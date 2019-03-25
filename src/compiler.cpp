#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

/*****************/
//TYPE DEFINITIONS
/*****************/

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

/*****************/
//MACRO
/*****************/

#define SET_BIT(raw, flag)   (raw |=   1 << flag)
#define RESET_BIT(raw, flag) (raw &= ~(1 << flag))
#define GET_BIT(raw, flag)   ((1 << flag) & raw)

#define RAM_START     0x0000
#define STACK_START   0x0800
#define ROM_START     0x7FFF

#define strequ(x, y)  !strcmp(x, y)

/*****************/
//OPCODES
/*****************/

#include "ops.h"

struct OP
{
    u8          opcode   = 0;
    u16         argument = 0;
    char        op_mode  = OP_MODE_NONE;
    std::string arg_id   = "";
};

/*****************/
//UTILITY FUNCTIONS
/*****************/

inline static u64 fsize(FILE* in)
{
    fseek(in, 0, SEEK_END);
    u64 size = ftell(in);
    fseek(in, 0, SEEK_SET);
    return size;
}

static bool cus_getline(std::string& buffer, FILE* in)
{
    buffer = "";
    int c;
    while((c = fgetc(in)) != '\n' && c != EOF)
    {
        buffer += (char)c;
    }
    buffer += '\n';
    return c != EOF;
}

int get_str_val(const char* str)
{
    int num = 0;
    
    //binary number
    if(str[0] == '%')
    {
        for(u8 i = 1; str[i] == '0' || str[i] == '1'; i++)
        {
            num = (num << 1) + !!(str[i] - '0');
        }
    }
    //hex number
    else if(str[0] == '$')
    {
        if(sscanf(str + 1, "%x", &num) != 1) { return -1; }
    }
    else
    {
        if(sscanf(str, "%u", &num) != 1) { return -1; }
    }
    
    return num;
}

/*****************/
//DECOMPILE
/*****************/

//decompiles code
void decompile(std::string source_path, std::string output_path)
{
    //open files
    FILE* in = fopen(source_path.c_str(), "rb");
    FILE* out = fopen(output_path.c_str(), "wb");
    
    if(in == NULL || out == NULL) { printf("error: cannot open files\n"); return; }
    
    //read input file
    u64 size = fsize(in);
    u8* buffer = (u8*)malloc(size);
    fread(buffer, sizeof(u8), size, in);
    
    //process instructions
    for(u64 i = 0; i < size; )
    {
        u8 op       = buffer[i];
        
        //check if byte is in the opcode range
        //TODO: if we gonna have 256 instructions, this becomes meaningless
        if(op > sizeof(OP_NAMES) / sizeof(OP_NAMES[0])) { i++; continue; }
        
        //print opcode name
        fprintf(out, "   %s ", OP_NAMES[op]);
        
        //print arguments
        u8 num_args = OP_ARGS[op];
        
        if(OP_MODES[op] == OP_MODE_VAL)
        {
            fprintf(out, "#0x");
        }
        
        for(u8 a = 0; a < num_args; a++)
        {
            fprintf(out, "%02x", buffer[i + a + 1]);
        }
        
        if(op == OPRAX_LDA) { fprintf(out, ",x"); }
        if(op == OPRAY_LDA) { fprintf(out, ",y"); }

        fprintf(out, "\n");
        
        //move onto the next instruction
        i += 1 + num_args;
    }
    
    //cleanup
    free(buffer);
    fclose(in);
    fclose(out);
}

/*****************/
//COMPILE
/*****************/

void compile(std::string source_path, std::string output_path)
{
    std::unordered_map<std::string, u16> constants; //define constants (macro)
    std::unordered_map<std::string, u16> variables; //define variables (label)
    
    std::unordered_map<std::string, u16> labels;    //define label
    
    std::vector<OP>                      program;   //define opcodes
    
    //open file
    FILE* in  = fopen(source_path.c_str(), "rb");
    
    if(in == NULL) { printf("cannot open source file: %s\n", source_path.c_str()); }
    
    
    //offsets
    u16 user_ram_offset = 0;
    
    u32 current_line    = 0;
    u32 current_byte    = 0;
    
#define add_opcode(op)\
        program.push_back(op);\
        if(op.op_mode == OP_MODE_ADD || op.op_mode == OP_MODE_UNRESOLVED_ADD)\
        {\
            current_byte += 3;\
        }\
        else if(op.op_mode == OP_MODE_VAL || op.op_mode == OP_MODE_UNRESOLVED_VAL)\
        {\
            current_byte += 2;\
        }\
        else\
        {\
            current_byte += 1;\
        }
    
#define err(str)      printf("error [line: %u]: %s\n", current_line + 1, str); exit(1);
    
    //line buffer
    std::string source;
    
    //first pass
    //decode macros and opcodes
    bool iterate = true;
    while(iterate)
    {
        //fetch line
        iterate = cus_getline(source, in);
        
        //processing character on line
        u32 i = 0;
        
        //skip comment or empty line
        if(source[i] == ';' || source.size() == 1 || source.size() == 0) { goto NEXT_LINE; }
        
        
        /*****************/
        //we expect opcode or user settings or comment
        /*****************/
        if(source[i] == ' ' || source[i] == '\t')
        {
            //skip white characters
            while(source[i] == ' ' || source[i] == '\t') { i++; }
            
            /**********************/
            //we expect user settings
            /**********************/
            if(source[i] == '.')
            {
                //set user ram offset
                if(source[i + 1] == 'o' && source[i + 2] == 'r' && source[i + 3] == 'g')
                {
                    i += 5;
                    
                    //fetch argument
                    //NOTE: between "org" and argument MUST be white character
                    int address = get_str_val(source.c_str() + i);
                    if(address == -1) { err(".org has invalid argument"); }
#ifdef DEBUG
                    printf("LOG: change ram offset: [0x%04x -> 0x%04x]\n", user_ram_offset, address);
#endif
                    user_ram_offset = (u16)address;
                    
                    goto NEXT_LINE;
                }
                
                //define variable
                if(source[i + 1] == 'd')
                {
                    if(source[i + 2] == 'b')
                    {
                        
                    }
                    if(source[i + 2] == 'w')
                    {
                        
                    }
                }
                
                //include binary data
                if(source[i + 1] == 'i' && source[i + 2] == 'n' && source[i + 3] == 'c' && source[i + 4] == 'b' && source[i + 5] == 'i' && source[i + 6] == 'n')
                {
                    i += 4;
                    while(source[i] != '"')
                    {
                        if(source[i] == '\n') { err("include expects file"); }
                        i++;
                    }
                    i++;
                    
                    std::string file_name = "";
                    
                    while(source[i] != '"')
                    {
                        if(source[i] == '\n') { err("include expects file"); }
                        file_name += source[i++];
                    }
                    
#ifdef DEBUG
                    printf("LOG: include file [%s]\n", file_name.c_str());
#endif
                    
                    //open requested file
                    FILE* inc_file = fopen(file_name.c_str(), "rb");
                    if(inc_file == NULL) { err("cannot include file"); }
                    
                    //load content into buffer
                    fseek(inc_file, 0, SEEK_END);
                    u32 inc_file_size = ftell(inc_file);
                    fseek(inc_file, 0, SEEK_SET);
                    
                    char* inc_buffer = (char*)malloc(inc_file_size);
                    
                    fread(inc_buffer, sizeof(char), inc_file_size, inc_file);
                    
                    //add binary data to the program
                    for(u32 i = 0; i < inc_file_size; i++)
                    {
                        OP op; op.opcode = inc_buffer[i];
                        
                        add_opcode(op);
                    }
                    
                    //cleanup
                    free(inc_buffer);
                    fclose(inc_file);
                    
                    goto NEXT_LINE;
                }
            }
            //skip comment and potentially empty line
            else if(source[i] == ';' || source[i] == '\n')
            {
                goto NEXT_LINE;
            }
            
            /**********************/
            //we expect valid opcode
            /**********************/
            else
            {
                //decode opcode
                int result = -1;
                for(u32 ops = 0; ops < sizeof(OP_NAMES) / sizeof(OP_NAMES[0]); ops++)
                {
                    //opcode is 3 bytes long
                    if((source[i + 0] == OP_NAMES[ops][0] || source[i + 0] == OP_NAMES[ops][0] + 32) &&
                       (source[i + 1] == OP_NAMES[ops][1] || source[i + 1] == OP_NAMES[ops][1] + 32) &&
                       (source[i + 2] == OP_NAMES[ops][2] || source[i + 2] == OP_NAMES[ops][2] + 32))
                    {
                        result = ops;
                        break;
                    }
                }
                
                if(result == -1) { err("unknown opcode"); }
                
                //setup op code
                OP op;
                op.opcode  = (u8)result;
                op.op_mode = OP_MODES[op.opcode];
                
                //check if opcode has any arguments
                if(OP_ARGS[result] != 0)
                {
                    //move onto the argument
                    //MARK: between opcode and argument must be 1 white character!
                    i += 4;
                    
                    //argument expects a address
                    if(OP_MODES[result] == OP_MODE_ADD)
                    {
                        if(source[i] == '#')
                        {
                            err("opcode expected address as its argument");
                        }
                        else
                        {
                            int address               = 0;
                            std::string identificator = "";
                            
                            //fetch literal address
                            if((address = get_str_val(source.c_str() + i)) == -1)
                            {
                                //address fetch failed, try to find a constant OR label
                                //fetch identificator
                                while(source[i] != ' ' && source[i] != '\t' && source[i] != '\n' && source[i] != ';')
                                {
                                    identificator += source[i++];
                                }
                                
                                if(identificator.size() == 0) { err("opcode expected argument"); }
                                
                                auto con_id_pos = constants.find(identificator);
                                auto lab_id_pos = labels.find(identificator);
                                
                                //found to identic identificators in constants and labels
                                if(con_id_pos != constants.end() && lab_id_pos != labels.end())
                                {
                                    err("same identificator for label and macro");
                                }
                                
                                //found identificator in constants
                                if(con_id_pos != constants.end()) { op.argument = con_id_pos->second; }
                                //found identificator in labels
                                else if(lab_id_pos != labels.end()) { op.argument = lab_id_pos->second; }
                                //haven't found anything, put it to unresolved
                                //after processing source
                                //we will process opcodes
                                else
                                {
                                    op.op_mode = OP_MODE_UNRESOLVED_ADD;
                                    op.arg_id  = identificator;
                                }
                                
                                //add opcode to the program
                                add_opcode(op);
                            }
                            else
                            {
                                //if fetch was successful add opcode to the program
                                op.argument = (u16)address;
                                add_opcode(op);
                            }
                        }
                    }
                    
                    
                    
                    //argument expects a value
                    else if(OP_MODES[result] == OP_MODE_VAL)
                    {
                        //for checking byte fetching
                        int fetch_high_low = 0;
                        
                        if(source[i] != '#')
                        {
                            //convert opcodes from value to address mode
                            //or fetch high byte or low byte of certain label
                            //NOTE: address mode opcodes must be after value mode
                            int address               = 0;
                            std::string identificator = "";
                            if((address = get_str_val(source.c_str() + i)) == -1)
                            {
                                //address fetch failed, try label
                                
                                //check byte fetching
                                if(source[i] == '<') { fetch_high_low =  1; i++; }
                                if(source[i] == '>') { fetch_high_low = -1; i++; }
                                
                                //fetch identificator
                                while(source[i] != ' ' && source[i] != '\n' && source[i] != ';' &&
                                      source[i] != ',') //<- loading relative address
                                {
                                    identificator += source[i++];
                                }
                                
                                //convert ImmVal LDA to RelAdd LDA
                                if(source[i] == ',' && op.opcode == OPIV_LDA && fetch_high_low == 0)
                                {
                                    if(source[i + 1] == 'X' || source[i + 1] == 'x')
                                    {
                                        op.opcode  = OPRAX_LDA;
                                        op.op_mode = OP_MODES[op.opcode];
                                    }
                                    else if(source[i + 1] == 'Y' || source[i + 1] == 'y')
                                    {
                                        op.opcode = OPRAY_LDA;
                                        op.op_mode = OP_MODES[op.opcode];
                                    }
                                    else
                                    {
                                        err("opcode expected second argument");
                                    }
                                }
                                
                                if(identificator.size() == 0) { err("opcode expected argument"); }
                                
                                auto con_id_pos = constants.find(identificator);
                                auto lab_id_pos = labels.find(identificator);
                                
                                //found to identic identificators in constants and labels
                                if(con_id_pos != constants.end() && lab_id_pos != labels.end())
                                {
                                    err("same identificator for label and macro");
                                }
                                
                                //found identificator in constants
                                if(con_id_pos != constants.end())
                                {
                                    address = con_id_pos->second;
                                    if(fetch_high_low ==  1) { op.argument = (u8)(address >> 8); }
                                    if(fetch_high_low == -1) { op.argument = (u8)(address >> 0); }
                                }
                                //found identificator in labels
                                else if(lab_id_pos != labels.end())
                                {
                                    address = lab_id_pos->second;
                                    if(fetch_high_low ==  1) { op.argument = (u8)(address >> 8); }
                                    if(fetch_high_low == -1) { op.argument = (u8)(address >> 0); }
                                    
                                }
                                //haven't found anything, put it to unresolved
                                //after processing source
                                //we will process opcodes
                                else
                                {
                                    if(fetch_high_low != 0)
                                    {
                                        op.arg_id  = fetch_high_low == 1 ? "<" + identificator : ">" + identificator;
                                        op.op_mode = OP_MODE_UNRESOLVED_VAL;
                                    }
                                    else
                                    {
                                        op.arg_id  = identificator;
                                        op.op_mode = OP_MODE_UNRESOLVED_ADD;
                                    }
#ifdef DEBUG
                                    printf("LOG: unresolved opcode with identificator [%s]\n", op.arg_id.c_str());
#endif
                                }
                            }
                            
                            //convert opcodes from value mode to address mode
                            switch(result)
                            {
                                //convert LDA
                                case OPIV_LDA:
                                {
                                    if(op.opcode != OPRAX_LDA && op.opcode != OPRAY_LDA && fetch_high_low == 0)
                                    {
                                        op.opcode   = OPIA_LDA;
                                        if(op.op_mode != OP_MODE_UNRESOLVED_ADD)
                                        {
                                            op.op_mode  = OP_MODES[op.opcode];
                                            op.argument = (u16)address;
                                        }
                                    }
                                    
                                    add_opcode(op);
                                    
                                    goto NEXT_LINE;
                                    break;
                                }
                                //cannot convert
                                default:
                                {
                                    printf("%i %u ", result, OP_MODES[result]); err("opcode expected value as its argument");
                                }
                            }
                        }
                        else
                        {
                            i++; //move onto the actual number
                            
                            int value                 = 0;
                            std::string identificator = "";
                            if((value = get_str_val(source.c_str() + i)) == -1)
                            {
                                //value fetch failed, try to find an constant
                                while(source[i] != ' ' && source[i] != '\t' && source[i] != '\n' && source[i] != ';')
                                {
                                    identificator += source[i++];
                                }

                                
                                auto id_pos = constants.find(identificator);
#ifdef DEBUG
                                printf("LOG: opcode has identificator as argument: [%s, %lu]\n", identificator.c_str(), identificator.size());
#endif
                                
                                if(id_pos == constants.end())
                                {
                                    err("opcode has argument undefined constant");
                                }
                                
                                if(id_pos->second & 0xFF00)
                                {
                                    err("opcode argument is too big [max: 255]");
                                }
                                
                                op.argument = (u8)id_pos->second;
                                add_opcode(op);
                                
                                goto NEXT_LINE;
                            }
                            else
                            {
                                //write immidiate value
                                op.argument = (u8)value;
                                add_opcode(op);
                                
                                goto NEXT_LINE;
                            }
                        }
                    }
                    else
                    {
                        err("opcode has arguments, but unknown mode");
                    }
                }
                else
                {
                    //add the opcode
                    add_opcode(op);
                    //move onto the next line
                    while(source[i++] != '\n') { }
                }
            }
        }
        
        
        
        //we expect macro
        else if(source[i] != ' ' && source[i] != '\t')
        {
            if(source[i] == ';') { goto NEXT_LINE; }
            
            std::string identificator = "";
            while(source[i] != ' ' && source[i] != '\t' && source[i] != '=' && source[i] != ':') { identificator += source[i++]; }
            
            bool found_valid_macro = false;
            
            while(source[i] != '\n')
            {
                //label TODO: you can have opcode after label
                if (source[i] == ':')
                {
#ifdef DEBUG
                    printf("LOG: Found label [%s, 0x%04x]\n", identificator.c_str(), user_ram_offset + current_byte);
#endif
                    labels.insert( { identificator, user_ram_offset + current_byte } );
                    
                    found_valid_macro = true;
                    break;
                }
                //constant
                else if(source[i] == '=')
                {
                    i++;
                    //skip white chars
                    while(source[i] == ' ' || source[i] == '\t') { i++; }
                    
                    //load constant to string
                    std::string str_constant = "";
                    while(source[i] != '\n' && source[i] != ' ' && source[i] != '\t' && source[i] != ';')
                    {
                        str_constant += source[i++];
                    }
                    
                    int constant;
                    if((constant = get_str_val(str_constant.c_str())) == -1)
                    {
                        err("constant macro expected value as its assignment");
                    }
                    
#ifdef DEBUG
                    printf("LOG: Found constant [%s, %hu]\n", identificator.c_str(), (u16)constant);
#endif
                    constants.insert( { identificator, (u16)constant } );
                    
                    found_valid_macro = true;
                    
                    break;
                }
                else
                {
                    i++;
                }
            }
            
            if(!found_valid_macro) { err("unfinished identificator"); }
            
            goto NEXT_LINE;
        }
        
    NEXT_LINE:
        current_line++;
    }
    
    fclose(in);
    
    //second pass
    //process opcodes and write final executable
    FILE* out = fopen(output_path.c_str(), "wb");
    
    //TODO: write header
    //TODO: calculate number of pages
    
    for(u32 i = 0; i < program.size(); i++)
    {
        OP op = program[i];
        switch(op.op_mode)
        {
            //in unresolved can be addresses or byte fetching
            case OP_MODE_UNRESOLVED_VAL:
            case OP_MODE_UNRESOLVED_ADD:
            {
                //fetching high or low byte of label
                int fetch_high_low = 0;
                if(op.arg_id[0] == '<' || op.arg_id[0] == '>')
                {
                    fetch_high_low = op.arg_id[0] == '<' ? 1 : -1;
                    op.arg_id      = op.arg_id.erase(0, 1);
                }
                //find constant or label
                auto con_id_pos = constants.find(op.arg_id);
                auto lab_id_pos = labels.find(op.arg_id);
                
                if(con_id_pos != constants.end() && lab_id_pos != labels.end())
                {
                    err("found same identificator for label and a macro");
                }
                
                //found identificator in constants
                if(con_id_pos != constants.end())
                {
                    if(fetch_high_low == 0)
                    {
                        op.argument = con_id_pos->second;
                    }
                    else
                    {
                        op.argument = fetch_high_low == 1 ? (u8)(con_id_pos->second >> 8) : (u8)(con_id_pos->second >> 0);
                    }
                }
                //found identificator in labels
                else if(lab_id_pos != labels.end())
                {
                    if(fetch_high_low == 0)
                    {
                        op.argument = lab_id_pos->second;
                    }
                    else
                    {
                        op.argument = fetch_high_low == 1 ? (u8)(lab_id_pos->second >> 8) : (u8)(lab_id_pos->second >> 0);
                    }
                }
                else
                {
                    err("opcode uses undefined macro");
                }
                
                fputc(op.opcode, out);
                if(fetch_high_low == 0) { fputc((u8)(op.argument >> 8), out); }
                fputc((u8)op.argument, out);
                
                break;
            }
                
            //write instruction with no arguments
            case OP_MODE_NONE:
            {
                fputc(op.opcode, out);
                break;
            }
            //write instruction with value argument
            case OP_MODE_VAL:
            {
                fputc(op.opcode,       out);
                fputc((u8)op.argument, out);
                break;
            }
            //write instruction with address argument
            case OP_MODE_ADD:
            {
                fputc(op.opcode, out);
                fputc((u8)(op.argument >> 8), out);
                fputc((u8)op.argument, out);
                break;
            }
        }
    }
    
    fclose(out);
}

/*****************/
//MAIN
/*****************/

int main(int argc, char* argv[])
{
    if(argc < 3)
    {
        printf("usage: com [-c/-d source] [-o output]\n"); exit(1);
    }
    
    std::string input;
    std::string output = "out";
    enum { NONE, COMPILE, DECOMPILE } mode = NONE;
    
    for(int i = 0; i < argc; i++)
    {
        if(strequ(argv[i], "-c"))
        {
            if(i + 1 == argc) { printf("error: missing source file\n"); exit(1); }
            input = argv[i + 1];
            mode  = COMPILE;
        }
        else if(strequ(argv[i], "-d"))
        {
            if(i + 1 == argc) { printf("error: missing source file\n"); exit(1); }
            input = argv[i + 1];
            mode  = DECOMPILE;
        }
        else if(strequ(argv[i], "-o"))
        {
            if(i + 1 == argc) { printf("error: missing source file\n"); exit(1); }
            output = argv[i + 1];
        }
    }
    
    if(mode == NONE)
    {
        printf("usage: com [-c/-d source] [-o output]\n"); exit(1);
    }
    else if(mode == COMPILE)
    {
        if(output == "out") { output += ".bin"; }
        
        compile(input, output);
    }
    else if(mode == DECOMPILE)
    {
        if(output == "out") { output += ".asm"; }
        
        decompile(input, output);
    }
}
