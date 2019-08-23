#include <stdint.h>
#include <stdio.h>

const uint64_t expmod ( const uint64_t b , const uint64_t n , const uint64_t p ) {
    __uint128_t a = 1;
    for ( uint64_t i = 0; i < n ; i ++)
        a = (a * b) % p;
    return a ;
}

int main(){
    printf (" 2^123456789 mod 18446744073709551533ULL = %lu \n " , expmod(2,123456789,18446744073709551533ULL) ) ;
    return 0;
}