#include<stdio.h> 
#include<unistd.h> 
#include<sys/types.h> 
#include<pthread.h> 
//gcc -g -Wall -pthread thread.c -o thread

int flag = 0;

//Declaração das threads
pthread_t thread_pai, thread_filha; 

//Variável para armazenar id da thread
pthread_t tmp_thread; 

//Função da thread pai
void* func(void* p){ 
    int contador = 0; 

    // cria thread_filha
    pthread_create(&thread_filha, NULL, func2, NULL); 

	while (1) { 
        
		printf("thread pai executando\n"); 
		sleep(1);
		contador++; 
		
		if (flag == 1) { 

			printf("thread filha teve sucesso\n"); 
            pthread_create(&thread_filha, NULL, func2, NULL); 

			//Faz a thread se matar
			//pthread_exit(NULL); 
	
    	}else if(contador == 10){
            //Mata a thread 
			pthread_cancel(tmp_thread); 
            pthread_create(&thread_filha, NULL, func2, NULL); 
        }
		
	} 
} 

//Função da thread filha
void* func2(void* p){ 

	// armazena id da thread atual
	tmp_thread = pthread_self(); 

    int contador = 0;

	while (1) { 
        int valor = rand() % (1) + 0;
        if(rand == 0){
            printf("thread filha executando normalmente\n"); 
            sleep(1);
        }else{
            printf("thread filha executando por muito tempo\n"); 
            sleep(12);
        }
        contador++;
        if(contador = 3){// simulando um caso de sucesso
	        flag = 1; //"retorna" uma informação
            pthread_exit(NULL); //se mata
        }
	} 

} 


int main(){ 

	// cria thread pai
	pthread_create(&thread_pai, NULL, func, NULL); 

	// waiting for when thread_one is completed 
	pthread_join(thread_pai, NULL); 

	// waiting for when thread_two is completed 
	pthread_join(thread_filha, NULL); 

}