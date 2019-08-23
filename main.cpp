#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <map>
#include <iostream>

// You probably want -std=gnu99 instead of -std=c99. C99 mode explicitly disables (most) GNU extensions.

// uint8_t code[] = {
//     0x55 ,                                              //push   rbp
//     0x48, 0x89, 0xe5,             	                    //mov    rbp,rsp
//     0x48, 0x89, 0x7d, 0xe8,          	                //mov    QWORD PTR [rbp-0x18],rdi
//     0x48, 0xc7, 0x45, 0xf0, 0x01, 0x00, 0x00, 0x00, 	//mov    QWORD PTR [rbp-0x10],0x1   
//     0x48, 0xc7, 0x45, 0xf8, 0x02, 0x00, 0x00, 0x00, 	//mov    QWORD PTR [rbp-0x8],0x2   
//     0xeb, 0x12,                	                        //jmp    1161 <fatorial_inter+0x2c>
//     0x48, 0x8b, 0x45, 0xf0,          	                //mov    rax,QWORD PTR [rbp-0x10]
//     0x48, 0x0f, 0xaf, 0x45, 0xf8,       	            //imul   rax,QWORD PTR [rbp-0x8]
//     0x48, 0x89, 0x45, 0xf0,          	                //mov    QWORD PTR [rbp-0x10],rax
//     0x48, 0x83, 0x45, 0xf8, 0x01,       	            //add    QWORD PTR [rbp-0x8],0x1
//     0x48, 0x8b, 0x45, 0xf8,          	                //mov    rax,QWORD PTR [rbp-0x8]
//     0x48, 0x3b, 0x45, 0xe8,          	                //cmp    rax,QWORD PTR [rbp-0x18]
//     0x76, 0xe4,                	                        //jbe    114f <fatorial_inter+0x1a>
//     0x48, 0x8b, 0x45, 0xf0,          	                //mov    rax,QWORD PTR [rbp-0x10]
//     0x5d,                   	                        //pop    rbp
//     0xc3,                   	                        //ret  
// };

// std::map <uint8_t, uint8_t> instruction_sizes = {
//     {0x55, 1},  // push
//     {0xc3, 1},  // ret
//     {0xEB, 2},  // jmp
//     {0x76, 2}  // jbe     
// };


// rdi = b , rsi = n , rdx = p
uint8_t code [] = {
    0x55 ,                                          // 0 push rbp
    0x48 , 0x89 , 0xE5 ,                            // 1 mov rbp, rsp
    0x48 , 0x89 , 0xD1 ,                            // 2 mov rcx, rdx (p)
    0x66 , 0xB8 , 0x01 , 0x00,                      // 3 mov(movi) ax, 1 (a=1)
    0x4D , 0x31 , 0xC0 ,                            // 4 xor r8, r8 (i=0)
    0x49 , 0x39 , 0xF0 ,                            // 5 cmp r8, rsi (i?n)
    0x0F , 0x83 , 0x11 , 0x00 , 0x00 , 0x00 ,       // 6 jae 0x11 (i>=n end)
    0x48 , 0xF7 , 0xE7 ,                            // 7 mul rdi (a = a * b)
    0x48 , 0xF7 , 0xF1 ,                            // 8 div rcx (rdx = a % p)
    0x48 , 0x89 , 0xD0 ,                            // 9 mov rax, rdx (rax = rdx)
    0x49 , 0xFF , 0xC0 ,                            // 10 inc r8 (i ++)
    0xE9 , 0xE6 , 0xFF , 0xFF , 0xFF ,              // 11 jmp  -0x1A (loop again) // 0x1110 0110 -> 0001 1010
    0x5D ,                                          // 12 pop rbp
    0xC3 ,                                          // 13 ret
};


/*
** Maximum size of x86 instructions are 15bytes
*/
struct Instruction {
    std::vector <uint8_t> instr;
    uint8_t size:4; //  15 = 1111, 4 bits
};
struct MetaDataJump{
    uint32_t src_line;
    uint32_t dest_line;
    int32_t rel_value;
};

std::map <uint8_t, uint8_t> instruction_sizes_map = {
    {0x55, 1},  // push reg
    {0xC3, 1},  // ret
    {0x5D, 1},  // pop reg
    {0x48, 3},  // mov reg ; div reg; mul reg
    {0x49, 3},  // inc regx; cmp regx, reg     
    {0x4D, 3},  // xor regx, regx
    {0x66, 4},  // movi 
    {0xE9, 5},  // jmp im32
    {0x0F, 6}  // jae im32
};

std::vector<MetaDataJump> jumps_metadata;


uint8_t getSizeOfInstruction(uint8_t opcode){
    return instruction_sizes_map[opcode];
}

void addSourceCodeToVector(uint8_t* sourcecode, std::vector<Instruction> &to_vector, uint32_t size){

    uint32_t i = 0, j = 0;
    while ( i < size){
        j = getSizeOfInstruction(sourcecode[i]);
        Instruction aux;

        aux.size = j;
        for(uint32_t k = 0; k < j; k++){
            aux.instr.push_back(sourcecode[i + k]);
        }

        to_vector.push_back(aux);

        i += j;
    }
}

void printInstructionVector(const std::vector<Instruction> &vec){

    for(std::size_t i = 0; i < vec.size() ; ++i){
        for (uint32_t j = 0; j < vec[i].instr.size(); j++){
            printf("%#2X ", vec[i].instr[j]);
        }
        printf("\n");
    } 
}


uint32_t mapJumpLocationsAux(const std::vector<Instruction> vec, uint32_t line, int32_t value){
    uint32_t sum = 0, currentline = line; 
    for(;;){
        if(sum > abs(value)){
            return currentline - 1;
        }
        else if(sum == abs(value)){
            return currentline + 1;

        }else{

            if(value > 0){ // verify whether the relative jump is forwards or backwards

                currentline++;
                sum += vec[currentline].size;
            }else{

                sum += vec[currentline].size;
                currentline--;
            }
        }
    }
    

}

void mapJumpLocations(const std::vector<Instruction> &vec){

    for (uint32_t line = 0; line < vec.size(); line++ ){

        std::vector<uint8_t> instr = vec[line].instr; 
        uint8_t opcode = instr[0];
        uint32_t size = instr.size();
        int32_t value = 0x0;
        MetaDataJump meta_aux;


        if (opcode == 0xE9){ // jmp
            for(uint32_t idx = 1; idx < size; idx++){
                value = value | ( instr[idx] << ((idx-1)*8));
            }
            meta_aux.src_line = line;
            meta_aux.dest_line = mapJumpLocationsAux(vec, line, value);
            meta_aux.rel_value = value;
            jumps_metadata.push_back(meta_aux);


        }else if (opcode == 0x0F) { // jae
            for(uint32_t idx = 2; idx < size; idx++){
                value = value | ( instr[idx] << ((idx-2)*8));
            }
            meta_aux.src_line = line;
            meta_aux.dest_line = mapJumpLocationsAux(vec, line, value);
            meta_aux.rel_value = value;
            jumps_metadata.push_back(meta_aux);
        }

    }

}


int main(){

    std::vector <Instruction> origin_vector;
    uint32_t length = sysconf ( _SC_PAGE_SIZE ) ;

    void * memory = mmap (NULL , length , PROT_NONE , MAP_PRIVATE | MAP_ANONYMOUS , -1 , 0);
    mprotect ( memory , length , PROT_WRITE );
    memcpy ( memory , ( void *) ( code ) , sizeof ( code ) );
    mprotect ( memory , length , PROT_EXEC );

    // uint32_t n = 10;
    // uint32_t (* jit ) ( uint32_t ) = ( uint32_t (*) ( uint32_t ) ) ( memory ) ;
    // printf ( " Fatorial de %d = %d \n" , n, (* jit ) (n) ) ;

    const uint64_t (* jit ) ( const uint64_t , const uint64_t , const uint64_t ) = 
        ( const uint64_t (*) ( const uint64_t , const uint64_t , const uint64_t ) ) ( memory ) ;
    // printf ( " 2^123456789 mod 18446744073709551533 = %lu \n" , (* jit ) (2 , 123456789 ,18446744073709551533ULL ) ) ;

    printf ( "2^12 mod 10 = %lu \n" , (* jit ) (2 , 12 ,10 ) ) ; // valor menor para usar enquanto testo

    addSourceCodeToVector(code, origin_vector, sizeof(code)/sizeof(uint8_t));

    // printInstructionVector(origin_vector);

    mapJumpLocations(origin_vector);

    for ( auto &i : jumps_metadata ) {
        // std::cout << "dest"i.dest_line +  << std::endl;
        printf("srcLine=%2d, destLine=%2d, value=%#.8X\n", i.src_line, i.dest_line, i.rel_value);
    }

    munmap ( memory , length ) ;

    return 0;
}

