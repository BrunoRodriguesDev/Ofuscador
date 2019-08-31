#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h> 
#include <stdlib.h>

int pid;

int main(){
    //union wait status;
    pid = fork();
    int processo = 0;

    if (-1 == pid){
        perror("Erro no fork.");
        return -1;
    }
    
    int tempo_inicial = time(NULL);

    if (pid == 0){
        for(;;){
            //sleep(1);
            printf("Sou um processo filho - louco e livre!!\n");
        }
    }

    if (pid > 0){
        processo = pid;
        printf("(%d)", pid);
        for(;;){
            printf("Eu sou um processo pai!\n");
            int tempo_atual = time(NULL);
            int diferenca_de_tempo = tempo_atual - tempo_inicial;
            if(diferenca_de_tempo > 3){
                printf("Adeus filho!\n");
                kill(processo, SIGTERM);
            }
        }
    }
    
    return 0;

}