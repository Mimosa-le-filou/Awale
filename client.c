/* Client pour les sockets
 *    socket_client ip_server port
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// #include <errno.h>
#include <unistd.h>

#define MAX_NAME 20


void print_board(){
	printf("\
	-------------------------------------\
	|     |     |     |     |     |     |\
	|     |     |     |     |     |     |\
	|-----------------------------------|\
	|     |     |     |     |     |     |\
	|     |     |     |     |     |     |\
	-------------------------------------\
	");
}


void check_command(){
	// Fonction permettant de vérifier l'intégrité des commandes utilisateurs
	}

int main(int argc, char** argv )
{
	// Déclarations
	int    sockfd,newsockfd,clilen,chilpid,ok,nleft,nbwriten;
	char c;
	struct sockaddr_in cli_addr,serv_addr;
	
	char player_name[MAX_NAME+1];
	char message[1024];
	char answer[1024];


	// Vérification de l'entrée 
	if (argc!=3) {printf ("usage: ./socket_client server_ip server_port\n");exit(0);}
	

	/*
	*  partie client 
	*/
	printf ("client starting\n");  

	/* initialise la structure de donnee */
	bzero((char*) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family       = AF_INET;
	serv_addr.sin_addr.s_addr  = inet_addr(argv[1]);
	serv_addr.sin_port         = htons(atoi(argv[2]));

	/* ouvre le socket */
	if ((sockfd=socket(AF_INET,SOCK_STREAM,0))<0)
	{printf("socket error\n");exit(0);}

	/* effectue la connection */
	if (connect(sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr))<0)
	{printf("socket error\n");exit(0);}


	// Enregistrement du client
	printf("Connexion au serveur réussie\n");

	// Envoi du pseudo et confirmation de réception
	printf("Quel est votre pseudo ? (taille max = 20 caractères) : ");
	int pseudo = 0;
	while(!pseudo){
		if(fgets(message, sizeof(message), stdin) != NULL){
			 message[strcspn(message, "\r\n")] = '\0';
			if(strlen(message) == 0 || strlen(message) > 20) continue;
			write(sockfd, message, strlen(message));
		}
		int n = read(sockfd, answer, sizeof(answer) - 1);
		if (n > 0) {
			answer[n] = '\0';
			printf("%s", answer);
			break;
		} else {
			printf("Connexion perdue avec le serveur.\n");
			break;
		}
	}


	// Boucle d'interactions avec le thread client côté serveur
	while(1){
		printf(">> ");
		fflush(stdout);

		if(fgets(message, sizeof(message), stdin) != NULL){
			 message[strcspn(message, "\r\n")] = '\0';
			if(strlen(message) == 0) continue;
			write(sockfd, message, strlen(message));
		}
		int n = read(sockfd, answer, sizeof(answer) - 1);
        if (n > 0) {
            answer[n] = '\0';
            printf("%s", answer);
        } else {
            printf("Connexion perdue avec le serveur.\n");
            break;
        }
	}

	close(sockfd);
	return 0;
}
