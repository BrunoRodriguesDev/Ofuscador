#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <ucontext.h>
#include <vector>
#include <map>
#include <iostream>

// #define _GNU_SOURCE /* Bring REG_XXX names from /usr/include/sys/ucontext.h */

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
    std::vector<Instruction> chromossome;
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


//Variables used in the logic of thre threads used
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_t threadRunner;
pthread_t threadWatcher;
uint8_t isThreadRunnerAlive;

typedef struct {
    uint8_t *ptr;
    uint32_t n;
} thread_arg_t, *ptr_thread_arg_t;

//

void printInstructionVector(const std::vector<Instruction> &vec);
void addSourceCodeToVector(uint8_t* sourcecode, std::vector<Instruction> &to_vector, uint32_t size);
void printInstructionVector(const std::vector<Instruction> &vec);
void remapJumpLocations(uint32_t newline, uint8_t nbytes, std::vector<Instruction> &vec, std::vector<MetadataJump> &jumps_metadata);
void copyVectorToArray(uint8_t *code2memory, std::vector<Instruction> &chromossome);
void executeInMemory(std::vector<Instruction> &chromossome);

void mutate(Chromossome &current);
void selectRandomGene(Instruction &aux, uint32_t random_line);

void *pthreadWaitOrKill(void* args);
void *pthreadExecuteInMemory(void* _args);
void *pthreadWaitWithTimeoutAndCond(void* args);

static void sigaction_sigfpe(int signal, siginfo_t *si, void *arg);

uint8_t getSizeOfInstruction(uint8_t opcode);
uint32_t getChromossomeSize(Chromossome &chromossome);
uint32_t mapJumpLocationsAux(const std::vector<Instruction> vec, uint32_t line, int32_t value);
inline uint32_t generateRandomNumber(uint32_t min, uint32_t max);

//

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
            printf("%.2X ", vec[i].instr[j]);
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
inline uint32_t generateRandomNumber(uint32_t min, uint32_t max){ 
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
    memcpy ( memory , ( void *) ( code2memory ) , sizeof(uint8_t)*chrom_size );
    mprotect ( memory , length , PROT_EXEC );

    // uint32_t n = 10;
    // uint32_t (* jit ) ( uint32_t ) = ( uint32_t (*) ( uint32_t ) ) ( memory ) ;
    // printf ( " Fatorial de %d = %d \n" , n, (* jit ) (n) ) ;

    const uint64_t (* jit ) ( const uint64_t , const uint64_t , const uint64_t ) = 
        ( const uint64_t (*) ( const uint64_t , const uint64_t , const uint64_t ) ) ( memory ) ;
    // printf ( " 2^123456789 mod 18446744073709551533 = %lu \n" , (* jit ) (2 , 123456789 ,18446744073709551533ULL ) ) ;

    printf ( "2^12 mod 10 = %lu \n" , (* jit ) (2 , 12 ,10 ) ) ; // valor menor para usar enquanto testo

    free(code2memory);
    munmap ( memory , length ) ;
}


void setSignalHanlder(int32_t signo){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = sigaction_sigfpe;
    sa.sa_flags = SA_SIGINFO;
    sigaction(signo, &sa, NULL);
}

void* pthreadExecuteInMemory(void* _args){

    isThreadRunnerAlive = 1;
    
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    setSignalHanlder(SIGFPE);
    usleep(100);

    ptr_thread_arg_t ptr_args = (ptr_thread_arg_t) _args;

    uint32_t length = sysconf ( _SC_PAGE_SIZE ) ;
    void * memory = mmap (NULL , length , PROT_NONE , MAP_PRIVATE | MAP_ANONYMOUS , -1 , 0);
    mprotect ( memory , length , PROT_WRITE );
    memcpy ( memory , ( void *) ( ptr_args->ptr ) , sizeof(uint8_t)*ptr_args->n );
    mprotect ( memory , length , PROT_EXEC );


    const uint64_t (* jit ) ( const uint32_t , const uint32_t , const uint64_t ) = 
        ( const uint64_t (*) ( const uint32_t , const uint32_t , const uint64_t ) ) ( memory ) ;
    // printf ( " 2^123456789 mod 18446744073709551533 = %lu \n" , (* jit ) (2 , 123456789 ,18446744073709551533ULL ) ) ;

    uint64_t retval = 0;
    retval = (*jit)(2, 12, 10);

    munmap ( memory , length ) ;
    printf("Terminei normal, com valor %d\n", retval);
    // pthread_cond_signal(&cond);
    isThreadRunnerAlive = 0;
    pthread_exit ( (void *) retval );
}


void *pthreadWaitOrKill(void* args){

    // clock_t start, end;
    // double cpu_time_used;
    time_t begin = NULL, end = NULL;
    
    begin = time(NULL);
    
    //     start = clock();
    
    while (true){
        // pthread_mutex_trylock(&mutex);
        // end = clock();
        usleep(100);
        end = time(NULL);

        // cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        if(isThreadRunnerAlive == 0 ){
            printf("Não precisou matar, com tempo %d\n", end-begin);
            break;
        }else if(end - begin >= 2){
            // uint32_t x =  pthread_cancel( *(pthread_t*) args );
            uint32_t x =  pthread_cancel( threadRunner );
            printf("Precisou matar, com tempo %d e valor %d\n", end-begin, x);
            break;
        }
        // pthread_mutex_unlock(&mutex);
        sched_yield();
    }
    pthread_exit(NULL);
}

void *pthreadWaitWithTimeoutAndCond(void* args){

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    // pthread_cond_init(&cond, NULL);
    pthread_mutex_lock(&mutex);
    uint32_t n = pthread_cond_timedwait(&cond, &mutex, &ts);
    pthread_mutex_unlock(&mutex);

    if (n == 0 || n == ETIMEDOUT){
        pthread_cancel( *(pthread_t*) args );
    }
    // pthread_cond_destroy(&cond);
    pthread_exit(NULL);
}

// Inserts in *aux a random instruction (gene) 
void selectRandomGene(Instruction &aux, uint32_t random_line){

    uint8_t reg_y, reg_x;
    do{
        reg_x = generateRandomNumber(1, 15);    // rax is avoided (0)
    }while(reg_x == 4 || reg_x == 5);   // rsp and rbp are also avoided

    do{
        reg_y = generateRandomNumber(1, 15);    // rax is avoided (0)
    }while(reg_y == 4 || reg_y == 5);   // rsp and rbp are also avoided
    
    uint32_t randomValue = generateRandomNumber(0, UINT32_MAX); // a random value to add when a IM32 is needed
    uint32_t randomInstruction = generateRandomNumber(0, 15);   // 15 is the number of decoded instructions 
    uint8_t ext;
    uint8_t regs;
    uint8_t byte;

    switch (randomInstruction){
        case 0: //INC reg
            //0100100X 11111111 11000XXX
            ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
            regs = (0b11000000) | ((reg_x & 0x7));
            aux.instr = {ext , 0xFF , regs };
        break;
        case 1: //DEC reg
            //0100100X 11111111 11001XXX
            ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
            regs = (0b11001000) | ((reg_x & 0x7));
            aux.instr = {ext , 0xFF , regs };
        break;

        case 2: //CMP reg, reg
            // 01001X0Y 00111001 11XXXYYY
            ext = (0b01001000) | ((reg_x & 0x8) >> 1) | ((reg_y & 0x8) >> 3 );
            regs = (0b11000000) | ((reg_x & 0x7) << 3) | (reg_y & 0x7);
            aux.instr = {ext , 0x39 , regs};
        break;
        
        case 3: // XOR reg, reg
            // 01001X0Y 00110001 11XXXYYY
            ext  = (0b01001000) | ((reg_x & 0x8) >> 1) | ((reg_y & 0x8) >> 3 );
            regs = (0b11000000) | ((reg_x & 0x7) << 3) | (reg_y & 0x7);
            aux.instr = {ext , 0x31 , regs};
        break;
        case 4: // XOR reg, im32
            if(reg_x == 1){
                aux.instr = {0x48 , 0x35};
            } else{
                ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
                regs = (0b11110000) | ((reg_x & 0x7));
                aux.instr = {ext , 0x81 , regs};
            }
            
            for(uint8_t idx = 0; idx < 4; idx++){
                byte = (uint8_t) ((0xFF000000 >> 8*idx) & randomValue)>>(8*(3-idx));
                aux.instr.push_back(byte);
            }
        break;

        case 5: //ADD reg, reg
            ext  = (0b01001000) | ((reg_x & 0x8) >> 1) | ((reg_y & 0x8) >> 3 );
            regs = (0b11000000) | ((reg_x & 0x7) << 3) | (reg_y & 0x7);
            aux.instr = {ext , 0x01 , regs};
        break;
        case 6: //ADD reg, im32
            if(reg_x == 1){
                aux.instr = {0x48 , 0x05};
            } else{
                ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
                regs = (0b11000000) | ((reg_x & 0x7));
                aux.instr = {ext , 0x81 , regs};
            }
            
            for(uint8_t idx = 0; idx < 4; idx++){
                byte = (uint8_t) ((0xFF000000 >> 8*idx) & randomValue)>>(8*(3-idx));
                aux.instr.push_back(byte);
            }
        break;

        case 7: //BSWAP reg - Byte Swap
            ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
            regs = (0b11001000) | ((reg_x & 0x7));
            aux.instr = {ext , 0x0F , regs };
        break;

        case 8: //NOT reg - One's Complement Negation
            ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
            regs = (0b11010000) | ((reg_x & 0x7));
            aux.instr = {ext , 0xF7 , regs };
        break;

        case 9: //NEG reg - Two's Complement Negation
            ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
            regs = (0b11011000) | ((reg_x & 0x7));
            aux.instr = {ext , 0xF7 , regs };
        break;

        case 10: //NEG reg - Two's Complement Negation
            ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
            regs = (0b11001000) | ((reg_x & 0x7));
            aux.instr = {ext , 0xFF , regs };
        break;

        case 11: //AND
            ext  = (0b01001000) | ((reg_x & 0x8) >> 1) | ((reg_y & 0x8) >> 3 );
            regs = (0b11000000) | ((reg_x & 0x7) << 3) | (reg_y & 0x7);
            aux.instr = {ext , 0x21 , regs};
        break;
        case 12: // AND reg, im32
            if(reg_x == 1){
                aux.instr = {0x48 , 0x0D};
            } else{
                ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
                regs = (0b11001000) | ((reg_x & 0x7));
                aux.instr = {ext , 0x81 , regs};
            }
            
            for(uint8_t idx = 0; idx < 4; idx++){
                byte = (uint8_t) ((0xFF000000 >> 8*idx) & randomValue)>>(8*(3-idx));
                aux.instr.push_back(byte);
            }
        break;

        case 13: //OR reg, reg
            ext  = (0b01001000) | ((reg_x & 0x8) >> 1) | ((reg_y & 0x8) >> 3 );
            regs = (0b11000000) | ((reg_x & 0x7) << 3) | (reg_y & 0x7);
            aux.instr = {ext , 0x09 , regs};
        break;
        case 14: // OR reg, im32

            if(reg_x == 1){
                aux.instr = {0x48 , 0x25};
            } else{
                ext  = (0b01001000) | ((reg_x & 0x8) >> 3);
                regs = (0b11100000) | ((reg_x & 0x7));
                aux.instr = {ext , 0x81 , regs};
            }
            
            for(uint8_t idx = 0; idx < 4; idx++){
                byte = (uint8_t) ((0xFF000000 >> 8*idx) & randomValue)>>(8*(3-idx));
                aux.instr.push_back(byte);
            }
        break;

        case 15: // CLC — Clear Carry Flag
            aux.instr = {0xF8};
        break;
    }

    aux.size = aux.instr.size();
}

void mutate(Chromossome &current){

        uint32_t random_line = generateRandomNumber(2, current.chromossome.size()-3);
        Instruction newGene;

        selectRandomGene(newGene, random_line);
        remapJumpLocations(random_line, newGene.size, current.chromossome, current.metadata);
        current.chromossome.insert(current.chromossome.begin() + random_line, newGene);
        
}

uint32_t getChromossomeSize(Chromossome &chromossome){
    uint32_t size = 0;
    for(uint32_t k = 0; k < chromossome.chromossome.size(); k++){
        size += chromossome.chromossome[k].size;
    }
    return size;
}

// void signals_callback_handler(int signumber){
//     if(signumber == SIGSEGV){
//         printf("DEU SIGSEGV\n");
//         exit(signumber);
//     }else if(signumber == SIGBUS){
//         printf("DEU SIGBUS\n");
//     }else if(signumber == SIGFPE){
//         printf("DEU SIGfpe\n");
//     }
//     pthread_cancel(threadRunner);
//     // 
// }


static void sigaction_sigfpe(int signal, siginfo_t *si, void *arg){
    ucontext_t *ctx = (ucontext_t *)arg;

    printf("Caught SIGFPE, addr %p, RIP 0x%llx\n", si->si_addr, ctx->uc_mcontext.gregs[REG_RIP]);
    pthread_cancel(pthread_self());
    isThreadRunnerAlive = 0;
    ctx->uc_mcontext.gregs[REG_RIP] += 6; // skipping the div regx instruction

}

int main(){

    // setSignalHanlder(SIGFPE);

    // signal(SIGSEGV, signals_callback_handler);
    // signal(SIGBUS, signals_callback_handler);
    // signal(SIGFPE, signals_callback_handler);
    FILE *file;

    std::vector <Instruction> origin_vector;
    std::vector <Chromossome> population_list;
    
    uint32_t bytes;
    void* retval = 0;
    uint32_t n = 0, i = 0;
    Chromossome aux;
    population_list.push_back(aux); // just so it initializes 

    file = fopen("code.hex", "r");
    if (file == NULL){ printf("Erro: nao foi possivel abrir o arquivo\n"); return 0; }
    else while ((fscanf(file, "%2x", &bytes)) != EOF) n++;

    rewind(file);
    uint8_t* origin_code = (uint8_t*) malloc(n*sizeof( uint8_t ));  
    while ((fscanf(file, "%2x", &bytes)) != EOF) origin_code[i++] = (uint8_t) bytes;
    fclose(file);

    addSourceCodeToVector(origin_code, population_list[0].chromossome, n);
    mapJumpLocations(population_list[0].chromossome, population_list[0].metadata );

    srand((uint32_t) time(0));

    uint32_t N_GENERATIONS = 20;
    uint32_t N_MUTATIONS = 3;
    uint32_t N_ALLOWED_GENES = 1;
    
    // repeat only for N_GENERATIONS
    for(uint32_t gen = 0; gen < N_GENERATIONS; gen++){

        std::vector<Chromossome> apt_list; 

        uint32_t popSize = population_list.size();

        // for each chromossome
        for (uint32_t i = 0; i < popSize; i++){

            // mutate N_MUTATIONS times
            for (uint32_t j = 0; j < N_MUTATIONS; j++){
                    Chromossome currentChromossome = population_list[i];

                    mutate(currentChromossome);
                    // printInstructionVector(currentChromossome.chromossome);

                    //thread related
                    uint32_t chrom_size = getChromossomeSize(currentChromossome);
                    uint8_t *code2memory = (uint8_t*) malloc(sizeof(uint8_t)*chrom_size); 
                    copyVectorToArray(code2memory, currentChromossome.chromossome);
                    thread_arg_t thread_args = {
                        .ptr = code2memory,
                        .n = chrom_size
                    };

                    isThreadRunnerAlive = 1;
                    pthread_create( &threadRunner, NULL, pthreadExecuteInMemory, &thread_args);
                    // pthread_create( &threadWatcher, NULL, pthreadWaitOrKill, (void*)(&threadRunner));
                    pthread_create( &threadWatcher, NULL, pthreadWaitOrKill, NULL);
                    pthread_join(threadRunner, &retval);
                    pthread_join(threadWatcher, NULL);
                    printf("\n");
                    
                    //thread related

                    // printf("retval: %lu\n",(uint64_t) retval );
                    // compares with the expected result
                    if((uint64_t) retval == 6){
                        apt_list.push_back(currentChromossome);
                        if(apt_list.size() >= N_ALLOWED_GENES){
                            break;
                        }
                    }
                    free(code2memory);

            }
        }

        // if there is any apt mutated chromossome, make them the new population since they have more instructions
        if(apt_list.size() > 0){
            population_list = apt_list;
            // printInstructionVector(population_list[0].chromossome);
        }
    }

    printf("Quantos deram certo: %lu \n", population_list.size());
    printInstructionVector(population_list[0].chromossome);
    executeInMemory(population_list[0].chromossome);

    free(origin_code);

    return 0;
}
