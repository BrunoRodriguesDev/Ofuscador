#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <vector>
#include <map>
#include <iostream>

//g++ -O3 -g -Wall -pthread algGenetico.cpp -o alg


#define N_GENES 1


// rdi = b , rsi = n , rdx = p
// uint8_t origin_code [] = {
//     0x55 ,                                          // 0 push rbp
//     0x48 , 0x89 , 0xE5 ,                            // 1 mov rbp, rsp
//     0x48 , 0x89 , 0xD1 ,                            // 2 mov rcx, rdx (p)
//     0x66 , 0xB8 , 0x01 , 0x00,                      // 3 mov(movi) ax, 1 (a=1)
//     0x4D , 0x31 , 0xC0 ,                            // 4 xor r8, r8 (i=0)
//     0x49 , 0x39 , 0xF0 ,                            // 5 cmp r8, rsi (i?n)
//     0x0F , 0x83 , 0x11 , 0x00 , 0x00 , 0x00 ,       // 6 jae 0x11 (i>=n end)
//     0x48 , 0xF7 , 0xE7 ,                            // 7 mul rdi (a = a * b)
//     0x48 , 0xF7 , 0xF1 ,                            // 8 div rcx (rdx = a % p)
//     0x48 , 0x89 , 0xD0 ,                            // 9 mov rax, rdx (rax = rdx)
//     0x49 , 0xFF , 0xC0 ,                            // 10 inc r8 (i ++)
//     0xE9 , 0xE6 , 0xFF , 0xFF , 0xFF ,              // 11 jmp  -0x1A (loop again) // 0x1110 0110 -> 0001 1010
//     0x5D ,                                          // 12 pop rbp
//     0xC3 ,                                          // 13 ret
// };
// The one-byte NOP instruction is an alias mnemonic for the XCHG (E)AX, (E)AX instruction.


/*
** Maximum size of x86 instructions are 15bytes
*/
struct Instruction {
    std::vector <uint8_t> instr;
    uint8_t size:4; //  15 = 1111, 4 bits
};

struct MetadataJump{
    uint32_t src_line;
    uint32_t dest_line;
    int32_t rel_value;
};

struct Chromossome{
    std::vector<Instruction> instructions;
    std::vector<MetadataJump> metadata;  
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
    {0x0F, 6}   // jae im32
};

//TODO: remover instrução unica, hardcoded e tamanho unico
const std::vector<uint8_t> gene_pool[N_GENES] = {
    {0x90}
};


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
        // printf("%d: ", i);
        for (uint32_t j = 0; j < vec[i].instr.size(); j++){
            printf("%#2X ", vec[i].instr[j]);
        }
        printf("\n");
    } 
}

//TODO: dar um nome mais intuitivo para essa funcao
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

//Executed only once
void mapJumpLocations(const std::vector<Instruction> &vec, std::vector<MetadataJump> &jumps_metadata){

    for (uint32_t line = 0; line < vec.size(); line++ ){

        std::vector<uint8_t> instr = vec[line].instr; 
        uint8_t opcode = instr[0];
        uint32_t size = instr.size();
        int32_t value = 0x0;
        MetadataJump meta_aux;


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

void remapJumpLocations(uint32_t newline, uint8_t nbytes, std::vector<Instruction> &vec, std::vector<MetadataJump> &jumps_metadata){

    for(uint32_t i = 0; i < jumps_metadata.size(); i++){

        //TODO: cada cromossomo vai precisar de seus metadados
        // MetadataJump aux = jumps_metadata[i];
        uint32_t src_line = jumps_metadata[i].src_line;
        uint32_t dest_line = jumps_metadata[i].dest_line;
        int32_t rel_value = jumps_metadata[i].rel_value;
        // printf("antes: jumps_metadata[%d] -> src=%d, dest=%d, value=%d \n",i, jumps_metadata[i].src_line, jumps_metadata[i].dest_line, jumps_metadata[i].rel_value);

        if (rel_value > 0){ // dest_line > src_line (a jump forwards)
            if(newline > src_line && newline <= dest_line){ // we need to alter dest_line and value

                //TODO: buscar a informacao correta do tamanho da instrucao (hardcoded por enquanto)
                uint8_t instr_size = instruction_sizes_map[0x0f];

                rel_value += nbytes;

                for(uint8_t idx = 0; idx < 4; idx++){
                    vec[src_line].instr[instr_size - idx - 1] = (uint8_t)((0xFF000000 >> 8*idx) & rel_value)>>(8*(3-idx));
                }

                jumps_metadata[i].dest_line++;
                jumps_metadata[i].rel_value = rel_value;

            }else if(newline <= src_line){ // if the instruction is before src_line than shift both source and destination by 1line
                jumps_metadata[i].src_line++;
                jumps_metadata[i].dest_line++;

            }
        }else{  // this means that src_line > dest_line (a jump backwards)
            if(newline <= src_line && newline > dest_line){

                //TODO: buscar a informacao correta do tamanho da instrucao (hardcoded por enquanto)
                uint8_t instr_size = instruction_sizes_map[0xe9];

                rel_value -= nbytes;

                for(uint8_t idx = 0; idx < 4; idx++){
                    vec[src_line].instr[instr_size - idx - 1] = ((0xFF000000 >> 8*idx) & rel_value)>>(8*(3-idx));
                }

                jumps_metadata[i].src_line++;
                jumps_metadata[i].rel_value = rel_value;

            }else if(newline <= dest_line){ // if the instruction is before dest_line than shift both source and destination by 1line
                jumps_metadata[i].src_line++;
                jumps_metadata[i].dest_line++;
            }
        }
        // printf("antes: jumps_metadata[%d] -> src=%d, dest=%d, value=%d \n",i, jumps_metadata[i].src_line, jumps_metadata[i].dest_line, jumps_metadata[i].rel_value);
    }
}

//Returns a random number between min and max ie. [min, max] 
uint32_t generateRandomNumber(uint32_t min, uint32_t max){ 
    return rand() % (max)  + min;
}

void copyVectorToArray(uint8_t *code2memory, std::vector<Instruction> &chromossome){

    uint32_t idx = 0; 
    for ( auto &elem : chromossome ) {

        uint8_t* pos = elem.instr.data();
        for(uint32_t i = 0; i < elem.instr.size(); i++){
            code2memory[idx++] = *pos++;
        }
    }
}

//Executes in memory a vector containing bytes correspondent to x86 instructions    
void executeInMemory(std::vector<Instruction> &chromossome){

    uint32_t chrom_size = 0;
    for(uint32_t i = 0; i < chromossome.size(); i++){
        chrom_size += chromossome[i].size;
    }

    uint8_t *code2memory = (uint8_t*) malloc(sizeof(uint8_t)*chrom_size); 

    copyVectorToArray(code2memory, chromossome);    

    uint32_t length = sysconf ( _SC_PAGE_SIZE ) ;
    void * memory = mmap (NULL , length , PROT_NONE , MAP_PRIVATE | MAP_ANONYMOUS , -1 , 0);
    mprotect ( memory , length , PROT_WRITE );
    memcpy ( memory , ( void *) ( code2memory ) , sizeof(code2memory)*chrom_size );
    mprotect ( memory , length , PROT_EXEC );

    // uint32_t n = 10;
    // uint32_t (* jit ) ( uint32_t ) = ( uint32_t (*) ( uint32_t ) ) ( memory ) ;
    // printf ( " Fatorial de %d = %d \n" , n, (* jit ) (n) ) ;

    const uint64_t (* jit ) ( const uint64_t , const uint64_t , const uint64_t ) = 
        ( const uint64_t (*) ( const uint64_t , const uint64_t , const uint64_t ) ) ( memory ) ;
    // printf ( " 2^123456789 mod 18446744073709551533 = %lu \n" , (* jit ) (2 , 123456789 ,18446744073709551533ULL ) ) ;

    printf ( "2^12 mod 10 = %lu \n" , (* jit ) (2 , 12 ,10 ) ) ; // valor menor para usar enquanto testo

    munmap ( memory , length ) ;
}


void* pthreadExecuteInMemory(void* args){

    std::vector<Instruction>* pointer = static_cast<std::vector<Instruction>*>(args);
    std::vector<Instruction> chromossome = *pointer;
    
    // std::vector<Instruction> &chromossome = std::vector<Instruction>*(args);
    printf("executando thread...\n");

    uint32_t chrom_size = 0;
    for(uint32_t i = 0; i < chromossome.size(); i++){
        chrom_size += chromossome[i].size;
    }

    uint8_t *code2memory = (uint8_t*) malloc(sizeof(uint8_t)*chrom_size); 

    copyVectorToArray(code2memory, chromossome);    

    uint32_t length = sysconf ( _SC_PAGE_SIZE ) ;
    void * memory = mmap (NULL , length , PROT_NONE , MAP_PRIVATE | MAP_ANONYMOUS , -1 , 0);
    mprotect ( memory , length , PROT_WRITE );
    memcpy ( memory , ( void *) ( code2memory ) , sizeof(code2memory)*chrom_size );
    mprotect ( memory , length , PROT_EXEC );


    const uint64_t (* jit ) ( const uint32_t , const uint32_t , const uint64_t ) = 
        ( const uint64_t (*) ( const uint32_t , const uint32_t , const uint64_t ) ) ( memory ) ;
    // printf ( " 2^123456789 mod 18446744073709551533 = %lu \n" , (* jit ) (2 , 123456789 ,18446744073709551533ULL ) ) ;

    uint64_t retval = (*jit)(2, 12, 10);

    printf ( "2^12 mod 10 = %lu \n" , retval ) ; // valor menor para usar enquanto testo

    munmap ( memory , length ) ;
    pthread_exit ( (void *) retval );
}


// Inserts in *aux a random instruction from the gene pool
void addRandomInstruction(Chromossome &chromossome, uint32_t random_gene, uint32_t random_line){

    Instruction aux;
    for (auto &byte: gene_pool[random_gene]){
        aux.instr.push_back(byte);
    }
    aux.size = aux.instr.size();

    chromossome.instructions.insert(chromossome.instructions.begin() + random_line, aux);
}


int main(){

  // for(auto &x: gene_pool){
  //     printf("size %d ", x.size());
  //     for(auto &&g : x){
  //         printf("%#x ", g);
  //     }
  //     printf("\n");
  // }

  FILE *file;


  std::vector <Instruction> origin_vector;
  std::vector <Chromossome> chromossome_list, chromossome_list_child, chromossome_temp;
  pthread_t thread;
  uint32_t bytes;
  void* status = 0;
  uint32_t n = 0, i = 0;
  Chromossome aux;
  chromossome_list.push_back(aux); // just so it initializes 
  chromossome_list_child.push_back(aux); 
  chromossome_temp.push_back(aux); 

  file = fopen("code.hex", "r");
  if (file == NULL){ printf("Erro: nao foi possivel abrir o arquivo\n"); return 0; }
  else while ((fscanf(file, "%2x", &bytes)) != EOF) n++;

  rewind(file);
  uint8_t* origin_code = (uint8_t*) malloc(n*sizeof( uint8_t ));
  // uint8_t origin_code[n];    
  while ((fscanf(file, "%2x", &bytes)) != EOF) origin_code[i++] = (uint8_t) bytes;
  fclose(file);

  // addSourceCodeToVector(origin_code, chromossome_list[0].instructions, sizeof(origin_code)/sizeof(uint8_t));
    addSourceCodeToVector(origin_code, chromossome_list[0].instructions, n);
    mapJumpLocations(chromossome_list[0].instructions, chromossome_list[0].metadata);

  srand((uint32_t) time(0));

  

  int filhos = 5;
  int nGeracoes = 50;

  int popSize = 1;
  int auxSize = 0;
  chromossome_list_child.resize(filhos);

  for (int i = 0; i < nGeracoes; i++){//gerações 
    for (int j = 0; j < chromossome_list.size(); j++){//cromossomos (tamanho da população, que pode ser aumentada)
      for (int k = 0; k < filhos; k++){//quantidade de filhos gerados para cada cromosomo

        chromossome_list_child[k] = chromossome_list[j];

        //seleciona uma linha aleatória
        uint32_t random_line = generateRandomNumber(1, chromossome_list_child[k].instructions.size()-1);

        //seleciona um gene aleatório
        uint32_t random_gene = generateRandomNumber(0, N_GENES);

        remapJumpLocations(random_line, gene_pool[random_gene].size(), chromossome_list_child[k].instructions, chromossome_list_child[k].metadata);
        addRandomInstruction( chromossome_list_child[k], random_gene, random_line);

        printf("\n/////////////////////////////////////////////////////////////////\n");
        printInstructionVector(chromossome_list_child[k].instructions);
        printf("\n");
        printf("Geração(%d) / Cromossomo(%d) / Variação(%d)\n", i+1, j+1, k+1);
        printf("/////////////////////////////////////////////////////////////////");
        printf("\n\n");
        
        pthread_create( &thread, NULL, pthreadExecuteInMemory, &chromossome_list_child[k]);
        pthread_join(thread, &status);

        printf("retval: %lu",(uint64_t) status );

        chromossome_temp.push_back(chromossome_list_child[k]); 
        auxSize++;

      }
        
    }
    popSize = popSize+auxSize;
    auxSize = 0;
    printf("\n/////////////////////////////////////////////////////////////////////Tamanho da população: %d\n", popSize);
    chromossome_list.resize(popSize);
    chromossome_list = chromossome_list_child;
  }

    return 0;
}
