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
#define MAX_TOTAL_GAMES 100


enum PLAYER_STATUS { // traiter le joueur comme une machine à états finis
    IN_MENU,
    IN_GAME,
    DISCONNECTED
};

typedef struct {
	struct tm* time;
	int player_id;
} challenge;


typedef struct {
	int game_id;
	int p1_id;
	int p2_id;
	int turn; // si 0 c'est le tour de p1, si 1 c'est p2;

	int board[12];
	int score_p1;
	int score_p2;


	int is_finished;
	int moves[100];
	pthread_mutex_t lock;
} game;


struct sockaddr_in cli_addr,serv_addr;


typedef struct {
	int id;
	int socket;
	char name[MAX_NAME];

	enum PLAYER_STATUS status;

	challenge challenges[5];
	int numb_chall;

	int active_game_indexes[MAX_TOTAL_GAMES];
	int active_number;
	int finished_game_indexes[MAX_TOTAL_GAMES];
	int finished_number;

	pthread_mutex_t lock;
	pthread_mutex_t write_lock;
} player;


typedef struct {
	player* list;
	uint player_connected;
	pthread_mutex_t lock;
} playerDataBase;


typedef struct {
	game* list;
	uint game_number;
	pthread_mutex_t lock;
} gameDataBase;

typedef struct {
	int playerId;
	playerDataBase* playerArgDB;
	gameDataBase* gameArgDB;
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


int print_pending_challenges(player* p, const player* playerList, char* buffer){
	buffer[0] ='\0';
	if(p->numb_chall == 0){
		strcat(buffer, "Aucun challenge en attente\n");
		return 1;
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
		strcat(buffer, "De qui voulez-vous accepter ou refuser la demande ([numéro du joueur](Y)/R/Q): ");
	}
	return 0;
}

void print_active_games(player *p, const gameDataBase* gDB, const playerDataBase* pDB, char* buffer){
	buffer[0] = '\0';
	if(p->active_number == 0){
		strcat(buffer, "Aucune parties actives\n");
	}
	else{
		strcat(buffer, "Parties actives:\n\n");
		char str[5];
		for(int i = 0; i < p->active_number; i++){
			strcat(buffer, "Partie n°");
			sprintf(str, "%d", p->active_game_indexes[i]);
			strcat(buffer, str);
			strcat(buffer, " contre : ");
			int p1 = gDB->list[p->active_game_indexes[i]].p1_id;
			char * opponent = (p->id == p1) ? pDB->list[gDB->list[p->active_game_indexes[i]].p2_id].name : pDB->list[gDB->list[p->active_game_indexes[i]].p1_id].name;
			strcat(buffer, opponent);
			strcat(buffer, "\n");
		}
		strcat(buffer, "----------\n");

	}

}


void send_to_player(player *target, const char *msg) {
    pthread_mutex_lock(&target->write_lock);
    
    if (target->status != DISCONNECTED) {
        write(target->socket, msg, strlen(msg));
    }
    
    pthread_mutex_unlock(&target->write_lock);
}


void init_game(game *g){
	g->is_finished = 0;
	for(int i = 0; i < 12; i++){
		g->board[i] = 4;
	}
}

/* Fonctions gestion du thread d'un joueur */
void *run(void *arg){
	// Variables de communication avec le client
	char message[1024];
	char answer[1024];

	thread_args *t = (thread_args*) arg;
	playerDataBase *playerDB = t->playerArgDB;
	player* playerList = playerDB->list;
	gameDataBase *gameDB = t->gameArgDB;
	player *p = &playerDB->list[t->playerId];
	
	//Réception du pseudo client et confirmation de réception
	memset(message, 0, sizeof(message));
	int n = read(p->socket, message, sizeof(message) -1);
	message[strcspn(message, "\r\n")] = '\0';
	strcpy(p->name, message);
	write(p->socket, "Pseudo enregistré !\n", strlen("Pseudo enregistré !\n"));
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
		
		// Pour écrire à un autre joueur
		char * token = strtok(answer, " ");
	
		switch(p->status){
			case IN_MENU: {
				if (!strcmp(answer, "1")){
					print_current_database(playerList, message);
					write(p->socket, message, strlen(message));
				}
				else if (!strcmp(answer, "2")){
					int empty = print_pending_challenges(p, playerList, message);
					write(p->socket, message, strlen(message));
					if(empty){break;}

					int canPlay = 0;
					int playerChallenged;

					// Attention à la gestion de l'acceptation du challenge ou non par p2
					player *p2;
					while(!canPlay){
						memset(answer, 0, sizeof(answer));
						int n = read(p->socket, answer, sizeof(answer)-1);
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
						playerChallenged = atoi(answer);
						p2 = &playerList[playerChallenged];
						if((playerChallenged < 0 || playerChallenged >= MAX_PLAYER) || playerChallenged == p->id 
							|| playerList[playerChallenged].status == DISCONNECTED){
							write(p->socket, "Erreur lors du choix du joueur, veuillez réesayer\n", strlen("Erreur lors du choix du joueur, veuillez réessayer\n"));
						}
						else{
							write(p->socket, "Vous avez répondu à un challenge, le jeu va se lancer...\n", strlen("Vous avez répondu à un challenge, le jeu va se lancer...\n"));
							canPlay = 1;
						}
					}
						// Création de la partie acceptée et affectation à la liste des parties pour les deux joueurs
						pthread_mutex_lock(&gameDB->lock);
						game *g = &gameDB->list[gameDB->game_number];
						g->game_id = gameDB->game_number++;
						g->p1_id = p->id;
						g->p2_id = p2->id;
						g->turn = rand()%2;
						pthread_mutex_unlock(&gameDB->lock);

						pthread_mutex_lock(&p->lock);
						p->active_game_indexes[p->active_number++] = g->game_id;
						pthread_mutex_unlock(&p->lock);

						pthread_mutex_lock(&p2->lock);
						p2->active_game_indexes[p2->active_number++] = g->game_id;
						pthread_mutex_unlock(&p2->lock);
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
						int already_challenged = 0;
						for(int i = 0; i < count; i++){
							if(playerList[i].challenges->player_id == p->id){
								write(p->socket, "Vous avez déjà défié ce joueur\n", strlen("Vous avez déjà défié ce joueur\n"));
								already_challenged = 1;
							}
						}
						if(already_challenged){break;}

						playerList[i].challenges[count].player_id = p->id;
						time_t now = time(NULL);
						playerList[i].challenges[count].time = localtime(&now);
						++playerList[i].numb_chall;
						pthread_mutex_unlock(&playerList[i].lock);
						send_to_player(p, "Défi correctement envoyé\n");
					}
					else{send_to_player(p, "Aucun joueur correspondant\n");}
				}
				else if (!strcmp(answer, "4")){
					print_active_games(p, gameDB, playerDB, message);
					write(p->socket, message, strlen(message));
				}
				else if (!strcmp(answer, token)){ // Envoi d'un message à un autre joueur
					printf("Entrée dans la bonne fonction\n");
					token = strtok(NULL, " ");
					int i = 0;
					while(i < MAX_PLAYER){
						if(!(strcmp(token, playerList[i].name))){break;}
						i++;
					}
					if(i == MAX_PLAYER){
						char str[60];
						strcat(str, "Le joueur [");
						strcat(str, token);
						strcat(str, "] n'existe pas dans la base de donnée");
						send_to_player(p, str);
						break;
					}
					message[0] = '\0';
					strcat(message, "Message reçu de [");
					strcat(message, p->name);
					strcat(message, "] : ");
					token = strtok(NULL, "");
					strcat(message, token);
					send_to_player(&playerList[i], message);
				}
				else{send_to_player(p, "Commande non reconnue\n");}
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




void initDb(playerDataBase *pDB, gameDataBase* gDB, int n, int m){
	pthread_mutex_init(&pDB->lock, NULL);
	pthread_mutex_init(&gDB->lock, NULL);	
	for(int i = 0; i < n; i++){
		pDB->list[i].status = DISCONNECTED;
		pthread_mutex_init(&pDB->list[i].lock, NULL);
		pthread_mutex_init(&pDB->list[i].write_lock, NULL);
	}
	for(int i = 0; i < m; i++){
		pthread_mutex_init(&gDB->list[i].lock, NULL);
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
	playerDataBase *playerDB = malloc(sizeof(playerDataBase)*MAX_TOTAL_GAMES);
	playerDB->player_connected = 0;

	playerDB->list = player_list;

	// Initialisation de la base de données des instances de jeu
	gameDataBase *gameDB = malloc(sizeof(gameDataBase));
	game *game_list = malloc(sizeof(game)*MAX_PLAYER);
	gameDB->list = game_list;
	gameDB->game_number = 0;
	initDb(playerDB, gameDB, MAX_PLAYER, MAX_TOTAL_GAMES);
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
		pthread_mutex_lock(&playerDB->lock);
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
		// player p = {
		// 	.id = player_id,
		// 	.socket = client_socket,
		// 	.status = IN_MENU,
		// 	.numb_chall = 0,
		// };
	
		// Pseudo-persistance du joueur dans la base données
		player_list[player_id].id = player_id;
		player_list[player_id].socket = client_socket;
		player_list[player_id].status = IN_MENU;
		player_list[player_id].numb_chall = 0;
		++playerDB->player_connected;
		pthread_mutex_unlock(&playerDB->lock);
		// Faire gaffe aux copies de mémoires
		// thread_args args = malloc(sizeof(thread_args));
		thread_args args;
		args.playerId = player_id;
		args.playerArgDB = playerDB;
		args.gameArgDB = gameDB;
		pthread_create(&threads[player_id], NULL, &run, (void *) &args);
	}
	close(sockfd);
	return 1;
 }
