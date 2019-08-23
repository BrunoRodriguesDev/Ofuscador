#include <stdio.h>
#include <stdint.h>

uint64_t fatorial_inter(uint64_t n) { 
    uint64_t r = 1; 
    for(uint64_t i = 2; i <= n; i++) 
        r=r*i; 
    return r; 
} 

int main(){  
    printf("%ld\n", fatorial_inter(10));
    return 0;
}
