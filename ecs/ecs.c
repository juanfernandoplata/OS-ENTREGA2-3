#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

#include "../lib/CLIENT/client.h"
#include "../lib/AGENT/agent.h"
#include "../lib/FORMATS/formats.h"

/* SE DEFINEN LOS PUERTOS DE LOS SERVIDORES */
#define AC_PORT 8000
#define SH_PORT 9000

/* SE DEFINE LA ESTRUCTURA SERVER PARA FACILITAR EL ORDEN DEL MANEJO DE VARIABLES */
typedef struct SERVER{
    int desc;
    struct sockaddr_in addr;
} SERVER;


/* ESTA ESTRUCTURA PERMITE PASAR MAS DE UN ARGUMENTO A UN HILO USANDO pthread_create() */
typedef struct ATT_ARGS{
    CLIENT * client;
    REQ * req;
} ATT_ARGS;

/* SE DECLARAN COMO VARIABLES GLOBALES EL DB DE LOS AGENTES, TERMINATE (FINALIZAR EJECUCION EN CASO DE FALLA) Y UN SEMAFORO PARA SYNC */
AGENTS_DB db;
int terminate = 0;
sem_t sem;

/* ESTA FUNCION INICIALIZA UN SERVIDOR TCP EN EL PUERTO port. LA FUNCION RETORNA -1 SE SE PRESENTA UN ERROR */
int initialize_tcp_server( SERVER * server, int port ){
    server->desc = socket( AF_INET, SOCK_STREAM, 0 );
    if( server->desc == -1 ){
        return -1;
    }
    server->addr.sin_family = AF_INET;
    server->addr.sin_addr.s_addr = INADDR_ANY;
    server->addr.sin_port = htons( port );
    if( bind( server->desc, ( struct sockaddr * ) &server->addr, sizeof( struct sockaddr_in ) ) == -1 ) {
        return -1;
    }
    if( listen( server->desc, 3 ) == -1 ){ // DEBERIA ESTANDARIZARSE
        close( server->desc );
        server->desc = -1;
        return -1;
    }
    return server->desc;
}

/* ESTA FUNCION HACE POLLING SOBRE UN SOCKET PARA VER SI EN UN TIEMPO MENOR A TIMEOUT (EN MILISEGUNDOS) SE PUEE EJECUTAR ALGUNA OPERACION DE I/O */
/* SOBRE EL SOCKET */
int socket_timeout( int desc, int ms ){
    struct pollfd target;
    target.fd = desc;
    target.events = POLLIN;
    return poll( &target, 1, ms );
}

/* ESTA FUNCION PERMITE RECIBIR DE FORMA SEGURA UN VALOR POR UN SOCKET (EXPLICAR) */
int safe_recv( AGENT * agent, REQ * res ){
    if( recv( agent->desc, res, sizeof( REQ ), 0 ) <= 0 ){
        close( agent->desc );
        agent->desc = -1;
        agent->connected = 0;
        return -1;
    }
    return 1;
}

/* ESTA FUNCION SE ENCARGA DE ATENDER LAS SOLICITUDES DEL CLIENTE ECS */
void attend( ATT_ARGS * args ){
    REQ res;
    int aux;
    int cont_index;
    int agent_index;

    CLIENT * client = args->client;
    REQ * req = args->req;

    switch( req->code ){
        case REQ_CREATE:
//            printf( "THREW REQ_CREATE" );
            if( CONTS_DB_alloc( &client->db ) == -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: client has no capacity...\n" );
                break;
            }
//            printf( "CHECKPOINT 1\n" );
            cont_index = CLIENT_has_container( client, req->data );
            if( cont_index > -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: container name is already in use...\n" );
                break;
            }
//            printf( "CHECKPOINT 2\n" );
            agent_index = AGENTS_DB_select( &db, &sem );
            if( agent_index == -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: there are no agents capable of attending request...\n" );
                break;
            }
//            printf( "CHECKPOINT 3\n" );
            if( !AGENT_is_connected( &db.agents[ agent_index ], &sem ) ){
                AGENT_connect( &db.agents[ agent_index ], &sem );
            }
//            printf( "CHECKPOINT 4\n" );
            if( !AGENT_is_connected( &db.agents[ agent_index ], &sem ) ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: could not stablish connection to agent...\n" );
                break;
            }
//            printf( "CHECKPOINT 5\n" );
            send( db.agents[ agent_index ].desc, req, sizeof( REQ ), 0 );
            int aux = socket_timeout( db.agents[ agent_index ].desc, TIMEOUT );
            if( aux == -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: request may have not been attended...\n" );
                break;
            }
//            printf( "CHECKPOINT 6\n" );
            if( aux == 0 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: agent response timedout, request may have not been attended...\n" );
                break;
            }
//            printf( "CHECKPOINT 7\n" );
            aux = recv( db.agents[ agent_index ].desc, &res, sizeof( REQ ), 0 );
            if( aux == -1 || aux == 0 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: connection to agent was lost during the transaction, request may have not been attended...\n" );
                break;
            }
//            printf( "CHECKPOINT 8\n" );
            if( res.code != REQ_ACK ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: agent ack was not recieved, request may have not been attended...\n" );  
            }
//            printf( "CHECKPOINT 9\n" );
            CLIENT_add_container( client, req->data, &db.agents[ agent_index ].addr );
            res.code = REQ_ACK;
            strcpy( res.data, "Success: container has been created...\n" );
//            printf( "CHECKPOINT 10\n" );
            break;
        case REQ_STOP:
            cont_index = CLIENT_has_container( client, req->data );
            if( cont_index == -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: container was not found...\n" );
                break;
            }
            if( CONT_get_status( &client->db.containers[ cont_index ] ) == 0 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: container is already stopped...\n" );
                break;
            }
            agent_index = AGENTS_DB_find( &db, &client->db.containers[ cont_index ].location, &sem );
            if( !AGENT_is_connected( &db.agents[ agent_index ], &sem ) ){
                AGENT_connect( &db.agents[ agent_index ], &sem );
            }
            if( !AGENT_is_connected( &db.agents[ agent_index ], &sem ) ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: could not stablish connection to agent...\n" );
                break;
            }
            send( db.agents[ agent_index ].desc, req, sizeof( REQ ), 0 );
            aux = socket_timeout( db.agents[ agent_index ].desc, TIMEOUT );
            if( aux == -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: request may have not been attended...\n" );
                break;
            }
            if( aux == 0 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: agent response timedout, request may have not been attended...\n" );
                break;
            }
            aux = recv( db.agents[ agent_index ].desc, &res, sizeof( REQ ), 0 );
            if( aux == -1 || aux == 0 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: connection to agent was lost during the transaction, request may have not been attended...\n" );
                break;
            }
            CLIENT_stop_container( client, cont_index );
            res.code = REQ_ACK;
            strcpy( res.data, "Success: container has been stopped...\n" );
            break;
        case REQ_DELETE:
            cont_index = CLIENT_has_container( client, req->data );
            if( cont_index == -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: container was not found...\n" );
                break;
            }
            if( CONT_get_status( &client->db.containers[ cont_index ] ) == 1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: can not remove container that is running...\n" );
                break;
            }
            agent_index = AGENTS_DB_find( &db, &client->db.containers[ cont_index ].location, &sem );
            if( !AGENT_is_connected( &db.agents[ agent_index ], &sem ) ){
                AGENT_connect( &db.agents[ agent_index ], &sem );
            }
            if( !AGENT_is_connected( &db.agents[ agent_index ], &sem ) ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: could not stablish connection to agent...\n" );
                break;
            }
            send( db.agents[ agent_index ].desc, req, sizeof( REQ ), 0 );
            aux = socket_timeout( db.agents[ agent_index ].desc, TIMEOUT );
            if( aux == -1 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: request may have not been attended...\n" );
                break;
            }
            if( aux == 0 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: agent response timedout, request may have not been attended...\n" );
                break;
            }
            aux = recv( db.agents[ agent_index ].desc, &res, sizeof( REQ ), 0 );
            if( aux == -1 || aux == 0 ){
                res.code = REQ_ERROR;
                strcpy( res.data, "Error: connection to agent was lost during the transaction, request may have not been attended...\n" );
                break;
            }
            CLIENT_delete_container( client, cont_index );
            res.code = REQ_ACK;
            strcpy( res.data, "Success: container has been deleted...\n" );
            break;
        case REQ_LIST:
            res.code = REQ_ACK;
            CLIENT_list_containers( client, res.data );
            send( client->desc, &res, sizeof( REQ ), 0 );
            break;
        default:
            res.code = REQ_ERROR;
            strcpy( res.data, "Error: request code is invalid...\n" );
            break;
    }
    send( client->desc, &res, sizeof( REQ ), 0 );
}

void ac_main_routine(){
    /* DECLARACION DE VARIABLES */
    SERVER ac_server;
    CLIENT client;
    int client_desc;
    struct sockaddr_in client_addr;
    int addr_size = sizeof( struct sockaddr_in );
    /* DECLARACION DE VARIABLES */
    printf( "AC MAIN: thread launched...\n" );
    if( initialize_tcp_server( &ac_server, AC_PORT ) == -1 ){
        sem_wait( &sem );
        terminate++;
        sem_post( &sem );
        printf( "AC MAIN: error on server initialization...\nAC MAIN: terminating thread...\n" );
        return;
    }
    printf( "AC MAIN: server is up...\n" );
    while( !terminate ){
        client_desc = accept( ac_server.desc, ( struct sockaddr * ) &client_addr, ( socklen_t * ) &addr_size );
        if( client_desc == -1 ){
            printf( "AC MAIN: error on client connection acceptance...\n" );
        }
        else{
            printf( "AC MAIN: client connection accepted...\n" );
            REQ req;
            pthread_t thread;
            ATT_ARGS att_args = { &client, &req };
            CLIENT_define( &client, client_desc, &client_addr );
            while( recv( client.desc, &req, sizeof( REQ ), 0 ) > 0 ){
                printf( "AC MAIN: client container request recieved...\n" );
                pthread_create( &thread, NULL, ( void * ) &attend, &att_args );
            }
            close( client.desc );
            printf( "AC MAIN: client connection lost...\nAC MAIN: awaiting for new client connection...\n" );
        }
    }
    close( ac_server.desc );
    return;
}

void sh_ping_routine(){
    /* DECLARACION DE VARIABLES */
    char * addr_str = malloc( INET_ADDRSTRLEN );
    socklen_t addr_size = sizeof( struct sockaddr_in );
    /* DECLARACION DE VARIABLES */
    printf( "SH PING: thread launched...\n" );
    while( !terminate ){
        for( int i = 0; i < MAX_AGENTS; i++ ){
            if( db.agents[ i ].desc > -1 ){
                if( AGENT_ping( &db.agents[ i ], &sem ) == -1 ){
                    if( AGENT_connect( &db.agents[ i ], &sem ) == -1 ){
                        printf( "SH PING: ping failed...\n" );
                    }
                    else{
                        if( AGENT_ping( &db.agents[ i ], &sem ) == -1 ){
                            printf( "SH PING: ping failed...\n" );
                        }
                        else{
                            printf( "SH PING: ping was succesful...\n" );
                        }
                    }
                }
                else{
                    printf( "SH PING: ping was succesful...\n" );
                }
                printf( "SH PING: target was -> %s\n", inet_ntop( AF_INET, &db.agents[ i ].addr, addr_str, ( socklen_t ) sizeof( struct sockaddr_in ) ) );
            }
        }
        sleep( 5 ); // ESTANDARIZAR
    }
    return;
}

void sh_main_routine(){
    /* DECLARACION DE VARIABLES */
    SERVER sh_server;
    REQ req, res;
    int agent_desc;
    struct sockaddr_in agent_addr;
    int addr_size = sizeof( struct sockaddr_in );
    /* DECLARACION DE VARIABLES */
    printf( "SH MAIN: thread launched...\n" );
    if( initialize_tcp_server( &sh_server, SH_PORT ) == -1 ){
        sem_wait( &sem );
        terminate++;
        sem_post( &sem );
        printf( "SH MAIN: error on server initialization...\nSH MAIN: terminating thread...\n" );
        return;
    }
    printf( "SH MAIN: server is up...\n" );
    while( !terminate ){
        agent_desc = accept( sh_server.desc, ( struct sockaddr * ) &agent_addr, ( socklen_t * ) &addr_size );
        if( agent_desc == -1 ){
            printf( "SH (CRITICAL): error on accept() call...\n" );
        }
        else{
            int status = socket_timeout( agent_desc, 10 * 1000 ); // 10 SEGUNDOS
            if( status == -1 ){
                printf( "SH (CRITICAL): error on poll() call...\n" );
            }
            else if( status > 0 ){
                status = recv( agent_desc, &req, sizeof( REQ ), 0 );
                if( status > 0 ){
                    if( req.code == REQ_CONNECT ){
                        printf( "SH: connection request recieved...\n" );
                        int i = AGENTS_DB_find( &db, ( struct sockaddr_in * ) &req.data, &sem );
                        if( i == -1 ){
                            printf( "SH: agent is not registered...\nSH: registering agent...\n" );
                            i = AGENTS_DB_alloc( &db, &sem );
                            if( i == -1 ){
                                res.code = REQ_ERROR;
                                strcpy( res.data, "Error: agent could not be allocated...\n" );
                                printf( "SH: agent could not be allocated...\n\n" );
                            }
                            else{
                                res.code = REQ_ACK;
                                strcpy( res.data, "Success: agent has been registered...\n" );
                                AGENT_set_addr( &db.agents[ i ], ( struct sockaddr_in * ) &req.data, &sem );
                            }
                        }
                        else{
                            res.code = REQ_ACK;
                            strcpy( res.data, "Success: agent is already registered in data base...\n" );
                            printf( "SH: agent is already registered in data base...\n" );
                        }
                        send( agent_desc, &res, sizeof( REQ ), 0 );
                        if( res.code == REQ_ACK && AGENT_connect( &db.agents[ i ], &sem ) != -1 ){
                            printf( "SH: agent registered...\n" );
                        }
                        else{
                            printf( "SH: agent could not be registered...\n" );
                        }
                    }
                }
            }
            close( agent_desc );
        }
    }
    close( sh_server.desc );
    return;
}

int main(){
    AGENTS_DB_init( &db );

    if( sem_init( &sem, 0, 1 ) == -1 ){
        printf( "MAIN (CRITICAL): error on sem_init() call...\nMAIN: terminating execution\n" );
        return -1;
    }

    pthread_t ac_thread, sh_main_thread, sh_ping_thread;
    
    pthread_create( &ac_thread, NULL, ( void * ) &ac_main_routine, NULL );
    pthread_create( &sh_main_thread, NULL, ( void * ) &sh_main_routine, NULL );
    pthread_create( &sh_ping_thread, NULL, ( void * ) &sh_ping_routine, NULL );
    
    pthread_join( ac_thread, NULL );
    pthread_join( sh_main_thread, NULL );
    pthread_join( sh_ping_thread, NULL );
    
    AGENTS_DB_term( &db );

    return 0;
}
