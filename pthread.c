/* Serveur sockets TCP
 * affichage de ce qui arrive sur la socket
 *    socket_server port (port > 1024 sauf root)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define MAX_NAME 20
#define MAX_PLAYER 100
enum PLAYER_STATUS { // traiter le joueur comme une machine à états finis
    IN_MENU,
    IN_GAME,
    DISCONNECTED
};

typedef struct {
	struct tm* time;
	int player_id;
} challenge;

struct sockaddr_in cli_addr,serv_addr;
typedef struct {
	int id;
	int socket;
	char name[MAX_NAME];
	enum PLAYER_STATUS status;
	challenge challenges[5];
	int numb_chall;
	pthread_mutex_t lock;
} player;

typedef struct {
	player* list;
	pthread_mutex_t lock;
	uint player_connected;
} playerDataBase;

typedef struct {
	int p1_id;
	int p2_id;
	int turn;
	pthread_mutex_t lock;
} game;

typedef struct {
	int playerId;
	playerDataBase* DB;
} thread_args;




void debug_print_bytes(const char *label, const char *str) {
    printf("%s: ", label);
    for (int j = 0; str[j] != '\0'; j++) {
        if ((unsigned char)str[j] >= 32 && (unsigned char)str[j] <= 126) {
            printf("'%c'(%d) ", str[j], (unsigned char)str[j]);
        } else {
            printf("[ASCII %d] ", (unsigned char)str[j]);
        }
    }
    printf("[\\0]\n");
}


// Appelée à la demande d'un joueur pour afficher la liste des joueurs connectés
void print_current_database(const player * database, char *buffer){
	buffer[0] = '\0';
	strcat(buffer,"Liste des joueurs connectés:\n\n");
	for(int i = 0; i < MAX_PLAYER; i++){
		if(database[i].status != DISCONNECTED){
			strcat(buffer, database[i].name);
			strcat(buffer, "\n");
		}
	}
}


void print_pending_challenges(player* p, const player* playerList, char* buffer){
	buffer[0] ='\0';
	pthread_mutex_lock(&p->lock);
	if(p->numb_chall == 0){
		strcat(buffer, "Aucun challenge en attente\n");
	}
	else{
		strcat(buffer, "Liste des challenges:\n\n");
		for(int i = 0; i < p->numb_chall; i++){
			strcat(buffer, "Défi reçu de : ");
			strcat(buffer, playerList[p->challenges[i].player_id].name);
			strcat(buffer, " à ");
			strcat(buffer, asctime(p->challenges[i].time));
			strcat(buffer, "\n");
		}
		strcat(buffer, "----------\n");
	}
	pthread_mutex_unlock(&p->lock);
}


/* Fonctions gestion du thread d'un joueur */
void *run(void *arg){
	// Variables de communication avec le client
	char message[1024];
	char answer[1024];

	thread_args *t = (thread_args*) arg;
	player *playerList = t->DB->list;
	player *p = &playerList[t->playerId];
	

	//Réception du pseudo client et confirmation de réception
	memset(message, 0, sizeof(message));
	int n = read(p->socket, message, sizeof(message) -1);
	message[strcspn(message, "\r\n")] = '\0';
	strcpy(p->name, message);

    pthread_t thisThread = pthread_self();
	printf("Thread [%lu] is running player %s of id %d\n", thisThread, p->name, p->id);
	
	while(1){
		memset(answer, 0, sizeof(answer));
		int n = read(p->socket, answer, sizeof(answer)-1);
	
		///// Procédure de déconnexion -- à implémenter
		// On attendra 1 minute la reconnexion puis on 
		// flush sa présence dans playerList et gameList
		if (n <= 0) {
			if (n == 0) {
				printf("Joueur %s déconnecté.\n", p->name);
			} else {
				perror("Erreur de lecture socket");
			}	
			close(p->socket);
			//cleanup_player(p, playerList, gameList);
			return NULL; // Interruption immédiate du thread
		}
		/////

		answer[strcspn(answer, "\r\n")] = '\0';
		printf("Message reçu : %s\n", answer);

	
		switch(p->status){
			case IN_MENU: {
				if (!strcmp(answer, "1")){
					print_current_database(playerList, message);
					write(p->socket, message, strlen(message));
				}
				else if (!strcmp(answer, "2")){
					print_pending_challenges(p, playerList, message);
					write(p->socket, message, strlen(message));
				}
				else if (!strcmp(answer, "3")){
					write(p->socket, "Qui voulez-vous défier ? \n", strlen("Qui voulez-vous défier ? \n"));
					int n = read(p->socket, answer, sizeof(answer));

					// à incapsuler dans une fonction
					if (n <= 0) {
						if (n == 0) {
							printf("Joueur %s déconnecté.\n", p->name);
						} else {
							perror("Erreur de lecture socket");
						}
						
						close(p->socket);
						//cleanup_player(p, playerList, gameList);    à implémenter
						return NULL; // Interruption immédiate du thread
					}
					//
					answer[strcspn(answer, "\r\n")] = '\0';
					printf("Joueur défié : %s\n", answer);
					
					int i = 0;
					while(i < MAX_PLAYER){
						if(playerList[i].status != DISCONNECTED){
							if(strcmp(playerList[i].name, answer) == 0){
								break;
							}
						}
						++i;
					}
					printf("%d\n", i);
					if(i < MAX_PLAYER){
						pthread_mutex_lock(&playerList[i].lock);
						int count = playerList[i].numb_chall;
						playerList[i].challenges[count].player_id = p->id;
						time_t now = time(NULL);
						playerList[i].challenges[count].time = localtime(&now);
						++playerList[i].numb_chall;
						//message[0] = '\0';
						//sprintf(message,"Vous avez été défié par le joueur : %s\n",p->name);
						//write(playerList[i].socket, message, strlen(message));
						pthread_mutex_unlock(&playerList[i].lock);
						write(p->socket, "Défi correctement envoyé\n", strlen("Défi correctement envoyé\n"));
					}
					else{write(p->socket, "Aucun joueur correspondant\n", strlen("Aucun joueur correspondant\n"));}
				}
				else{write(p->socket,"Commande non reconnue\n", strlen("Commande non reconnue\n"));}
				break;
			}

			case IN_GAME: {
				break;
			}

			case DISCONNECTED: {
				printf("Wsh\n");
				break;
			}

			default : {
				fprintf(stderr, "Unknown state for player : %d, %s\n", p->id, p->name);
				break;
			}
		}
	}
	free(t);
	return NULL;
}




void initDb(player *list, int n){
	for(int i = 0; i < n; i++){
		list[i].status = DISCONNECTED;
		pthread_mutex_init(&list[i].lock, NULL);
	}
}



/* Fonctions de gestion d'une partie */
int create_game(int player_id1, player player);
// Crée une partie dans la gestion 


int main(int argc, char** argv )
{
	// Empêcher le crash du serveur dans le cas d'un write à un client déconnecté
	signal(SIGPIPE, SIG_IGN);
	// Déclarations
	char datas[] = "hello\n";
	int    sockfd,newsockfd,chilpid,ok,nleft,nbwriten;
	socklen_t clilen = sizeof(cli_addr);
	char buffer[1024];
	

	// Concernant les threads
	pthread_t threads[MAX_PLAYER];

	// Initialisation de la *base de données des joueurs
	player *player_list = malloc(sizeof(player)*MAX_PLAYER);
	playerDataBase *playerDB = malloc(sizeof(playerDataBase));
	playerDB->player_connected = 0;
	initDb(player_list, MAX_PLAYER);
	playerDB->list = player_list;

	// Initialisation de la base de données des instances de jeu
	game *game_list = malloc(sizeof(game)*MAX_PLAYER);


	// Erreur d'arguments d'entrée
	if (argc!=2) {printf ("usage: socket_server port\n");exit(0);}
	
	printf ("server starting...\n");
	
	/* ouverture du socket */
	sockfd = socket (AF_INET,SOCK_STREAM,0);
	if (sockfd<0) {printf ("impossible d'ouvrir le socket\n");exit(0);}

	/* initialisation des parametres */
	bzero((char*) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family       = AF_INET;
	serv_addr.sin_addr.s_addr  = htonl(INADDR_ANY);
	serv_addr.sin_port         = htons(atoi(argv[1]));

	/* effectuer le bind */
	if (bind(sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr))<0)
	{printf ("impossible de faire le bind\n");exit(0);}

	/* petit initialisation */
	listen(sockfd,2);


	/* attend la connection d'un client */
	while (1)
	{
		//Nouvelle connexion d'un client
		int client_socket = accept(sockfd,(struct sockaddr*) &cli_addr,&clilen);
		if (client_socket<0) {printf ("accept error\n"); exit(0);}
		printf ("connection accepted\n");


		// Allocation de l'id et vérification de la saturation de serveur
		int player_id;
		int i = 0;
		for(i; i < MAX_PLAYER; i++){
			if(player_list[i].status == DISCONNECTED){
				player_id = i;
				break;
			}
		}
		if(i == MAX_PLAYER){
			char satured = 1;
			write(client_socket, &satured, 1);
			close(client_socket);
			continue;
		}
		else {
			char satured = 0;
			write(client_socket, &satured, 1);	
		}

		// Création d'un joueur statique
		player p = {
			.id = player_id,
			.socket = client_socket,
			.status = IN_MENU,
			.numb_chall = 0,
		};
	
		// Pseudo-persistance du joueur dans la base données
		pthread_mutex_lock(&playerDB->lock);
		memcpy(&player_list[player_id], &p, sizeof(player));
		++playerDB->player_connected;
		pthread_mutex_unlock(&playerDB->lock);
		write(client_socket, "Pseudo enregistré !\n", 22);
		// Faire gaffe aux copies de mémoires
		thread_args *args = malloc(sizeof(thread_args));
		args->playerId = player_id;
		args->DB = playerDB;
		pthread_create(&threads[player_id], NULL, &run, (void *) args);
	}
	close(sockfd);
	return 1;
 }
