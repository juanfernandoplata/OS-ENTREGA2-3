#ifndef AGENT_H
#define AGENT_H

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <semaphore.h>

#include <errno.h>

#include "../FORMATS/formats.h"

#define MAX_AGENTS 10
#define MAX_CONTS_PER_AGENT 10

#define MAX_TRIES 4
#define TIMEOUT 250

typedef struct AGENT{
    int desc;
    struct sockaddr_in addr;
    int connected;
    int containers;
} AGENT;

typedef struct AGENTS_DB{
    AGENT agents[ MAX_AGENTS ];
} AGENTS_DB;

// AGENT
void AGENT_set_desc( AGENT * this, int desc, sem_t * sem );

void AGENT_set_addr( AGENT * this, struct sockaddr_in * addr, sem_t * sem );

int AGENT_is_connected( AGENT * this, sem_t * sem );

int AGENT_match( AGENT * this, struct sockaddr_in * addr );

int AGENT_connect( AGENT * this, sem_t * sem );

int AGENT_ping( AGENT * this, sem_t * sem );
// AGENT

// AGENT_DB
void AGENTS_DB_init( AGENTS_DB * this );

int AGENTS_DB_alloc( AGENTS_DB * this, sem_t * sem );

int AGENTS_DB_select( AGENTS_DB * this, sem_t * sem );

int AGENTS_DB_find( AGENTS_DB * this, struct sockaddr_in * addr, sem_t * sem );

void AGENTS_DB_term( AGENTS_DB * this );
// AGENT_DB

#endif
