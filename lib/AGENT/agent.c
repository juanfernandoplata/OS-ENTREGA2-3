#include "./agent.h"

void AGENT_set_desc( AGENT * this, int desc, sem_t * sem ){
    sem_wait( sem );
    this->desc = desc;
    sem_post( sem );
}

void AGENT_set_addr( AGENT * this, struct sockaddr_in * addr, sem_t * sem ){
    sem_wait( sem );
    this->addr = * addr;
    sem_post( sem );
}

int AGENT_is_connected( AGENT * this, sem_t * sem ){
    int ret_val;
    sem_wait( sem );
    ret_val = this->connected;
    sem_post( sem );
    return ret_val;
}

int AGENT_match( AGENT * this, struct sockaddr_in * addr ){
    return this->addr.sin_family == addr->sin_family &&
                  this->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
                  this->addr.sin_port == addr->sin_port;
}

int AGENT_connect( AGENT * this, sem_t * sem ){
    int desc = socket( AF_INET, SOCK_STREAM, 0 );
    if( desc == -1 ){
        sem_wait( sem );
        this->desc = -1;
        this->connected = 0;
        sem_post( sem );
        printf( "AGENT LIB (CRITICAL): error on socket() call...\n" );
        return -1;
    }
    if( connect( desc, ( struct sockaddr * ) &this->addr, sizeof( struct sockaddr_in ) ) == -1 ){
        close( desc );
        sem_wait( sem );
        this->desc = -1;
        this->connected = 0;
        sem_post( sem );
        printf( "AGENT LIB (CRITICAL): error on connect() call...\n" );
        return -1;
    }
    sem_wait( sem );
    this->desc = desc;
    this->connected = 1;
    sem_post( sem );
    return 1;
}

int AGENT_ping( AGENT * this, sem_t * sem ){
    int desc;
    REQ req, res;
    struct pollfd target;

    sem_wait( sem );
    desc = this->desc;
    sem_post( sem );

    req.code = REQ_PING;
    target.fd = desc;
    target.events = POLLIN;
    send( desc, &req, sizeof( REQ ), 0 );
    if( poll( &target, 1, TIMEOUT ) <= 0 ){
        sem_wait( sem );
        this->connected = 0;
        sem_post( sem );
        return -1;
    }
    if( recv( desc, &res, sizeof( REQ ), 0 ) <= 0 ){
        close( desc );
        sem_wait( sem );
        this->desc = -1;
        this->connected = 0;
        sem_post( sem );
        return -1;
    }
    if( res.code != REQ_ACK ){
        sem_wait( sem );
        this->connected = 0;
        sem_post( sem );
        return -1;
    }
    sem_wait( sem );
    this->connected = 1;
    sem_post( sem );
    return 1;
}

void AGENTS_DB_init( AGENTS_DB * this ){
    for( int i = 0; i < MAX_AGENTS; i++ ){
        this->agents[ i ].desc = -1;
        this->agents[ i ].connected = 0;
        this->agents[ i ].containers = 0;
    }
}

int AGENTS_DB_alloc( AGENTS_DB * this, sem_t * sem ){
    int i = MAX_AGENTS - 1;
    sem_wait( sem );
    while( i >= 0 ){
        if( this->agents[ i ].desc == -1 ){
            break;
        }
        i--;
    }
    sem_post( sem );
    return i;
}

int AGENTS_DB_select( AGENTS_DB * this, sem_t * sem ){
    int i = -1;
    int low = MAX_CONTS_PER_AGENT;
    sem_wait( sem );
    for( int j = 0; j < MAX_AGENTS; j++ ){
        if( this->agents[ j ].desc > -1 && this->agents[ j ].containers < low ){
            i = j;
            low = this->agents[ j ].containers;
        }
    }
    this->agents[ i ].containers++;
    sem_post( sem );
    return i;
}

int AGENTS_DB_find( AGENTS_DB * this, struct sockaddr_in * addr, sem_t * sem ){
    int i = MAX_AGENTS - 1;
    sem_wait( sem );
    while( i >= 0 ){
        if( AGENT_match( &this->agents[ i ], addr ) ){
            break;
        }
        i--;
    }
    sem_post( sem );
    return i;
}

void AGENTS_DB_term( AGENTS_DB * this ){
    for( int i = 0; i < MAX_AGENTS; i++ ){
        if( this->agents[ i ].desc > -1 ){
            close( this->agents[ i ].desc );
        }
    }
}