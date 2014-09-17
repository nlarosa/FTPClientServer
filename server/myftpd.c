
/*

Nicholas LaRosa
CSE 30264, Project 1, Server

usage: myftpd <Port_Number>

*/

#include <sys/socket.h>
#include <sys/time.h>

#include <mhash.h>

#include <netdb.h>
#include <netinet/in.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>

#define BUFFER 1024
#define HASH_SIZE 16

int main( int argc, char** argv )
{
	int sockListen, sockConnect, isHost, i, err, willAccept;
	int inputBytes, bytes, outputBytes, totalBytes;
	struct sockaddr_in serverAddress;
	struct sockaddr_in * intermediate;
	struct sockaddr_storage clientAddress;
	struct addrinfo *hostInfo, *p;
	socklen_t socketSize;
	unsigned char * ipAddress;
	
	char sendLine[ BUFFER + 1 ];
	char recvLine[ BUFFER + 1 ];
	char ipstr[INET6_ADDRSTRLEN];

	char * filename;

	uint16_t filenameSize;		 	// client sends the size of the requested file name
	uint32_t fileSize;			// client recieves the size of the requested file
	unsigned int length;			// length of bytes written to a file	

	struct timeval startTimer;		// keep structs for recording timestamp at start and end of transfer
	struct tm * startTimeLocal;
	struct timeval endTimer;
	struct tm * endTimeLocal;	

	MHASH myHash;					// prepare for the md5 hashing of file retrieved
	unsigned char * readHash;	
	unsigned char * computeHash;			// have separate character arrays for hash retrieved and hash computed after retrieval
	unsigned char hashBuffer;	

	if( argc != 2 )
	{
		printf("\nusage: myftpd <Port_Number>\n\n");
		exit( 1 );
	}
	
	if( ( sockListen = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
	{
		perror( "Server - socket() error" );
		exit( 1 );
	}

	memset( ( char * )&serverAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons( atoi( argv[1] ) );			// and receive server port

	if( bind( sockListen, ( struct sockaddr * )&serverAddress, sizeof( struct sockaddr_in) ) < 0 )
	{
		perror( "Server - bind() error" );
		exit( 1 );
	}
		
	if( listen( sockListen, 1024 ) < 0 ) 			// listen to up to 1024 connections
	{
		perror( "Server - listen() error" );
		exit( 1 );
    	}

	while( 1 )								// continue until server is ended
	{
		socketSize = sizeof( clientAddress );
	
		if ( ( sockConnect = accept( sockListen, ( struct sockaddr * )&clientAddress, &socketSize ) ) < 0 )	// wait for connection, and then accept it
		{
			perror( "Server - accept() error" );
			exit( 1 );
		}

		intermediate = ( struct sockaddr_in * )&clientAddress;
		ipAddress = ( unsigned char * )&intermediate->sin_addr.s_addr;

		willAccept = 0;						// determines if we should connect to the IP

		if( ipAddress[0] == 127 )
		{
			willAccept = 1;
		}
		else if( ipAddress[0] == 129 && ipAddress[1] == 74 )
		{
			willAccept = 1;
		}
		else if( ipAddress[0] == 192 && ipAddress[1] == 168 )
		{
			willAccept = 1;
		}

		if( !willAccept )
		{
			printf( "\nClients must be from localhost or private networks.\n" );
			close( sockConnect );
			continue;
		}	

		if( read( sockConnect, recvLine, BUFFER ) <= 0 )		// first recieve the filename size (two bytes) and the filename
		{
			perror( "Server - read() error" );
			exit( 1 );
		}	

		memcpy( &filenameSize, recvLine, sizeof( uint16_t ) );		// store the first two bytes as a 16-bit integer

		filenameSize = ntohs( filenameSize );				// convert this to host long

		//printf( "Filename size is %d bytes.\n", filenameSize );

		recvLine[ sizeof( uint16_t ) + filenameSize ] = '\0';		// and then store those next amount of bytes as the filename
	
		filename = &recvLine[ sizeof( uint16_t ) ];
	
		//printf( "File name is %s.\n", filename );			
	
		FILE *filePtr = fopen( filename, "r" );                         // prepare to read and write to file
		if( filePtr == NULL )
                {
                        close( sockConnect );					// if file doesn't exist, remove the connection
                	continue;						// and skip the rest of this iteration
		}
		else								// otherwise, find the filesize using stat
		{
			fseek( filePtr, 0, SEEK_END );				
			fileSize = ftell( filePtr ); 				// get number of bytes
			fseek( filePtr, 0, SEEK_SET );				// reset file pointer to beginning
		}
	
		//printf( "The size of this file is %d bytes.\n", fileSize );	

		fileSize = htonl( fileSize );					// convert the byte order to that of the network

		memcpy( sendLine, &fileSize, sizeof( uint32_t ) );		// place the size in a character buffer to be sent

		fileSize = ntohl( fileSize );					// switch back

		if( ( outputBytes = write( sockConnect, sendLine, sizeof( uint32_t ) ) ) <= 0 )
        	{
			perror( "Server - write() error" );
			exit( 1 );
        	}

		myHash = mhash_init( MHASH_MD5 );                               // prepare to perform the MD5 hashing function
                if( myHash == MHASH_FAILED )
                {
                        perror( "Server - mhash() error" );
			exit( 1 );
                }

                inputBytes = 0;                                                 // inputBytes is the number of bytes in the current buffer
                totalBytes = 0;

		while( totalBytes < fileSize )					// first runthrough of the file computes hash
		{
			if( fileSize > BUFFER ) 						// fill the rest of the buffer if our file is too big for a single
			{
				if( ( bytes = fread( sendLine + inputBytes, sizeof( char ), BUFFER - inputBytes, filePtr ) ) <= 0 )	// transfer just enough to fill the buffer
				{
					perror( "Error reading from file" );					// close the socket if we recieve zero
					close( sockConnect );
					exit( 1 );
				}
			}
			else                                                                            // if our file will not overflow the buffer
			{
				if( ( bytes = fread( sendLine + inputBytes, sizeof( char ), fileSize - inputBytes, filePtr ) ) <= 0 )	// transer the entire file into the buffer
				{
					perror( "Error reading from file" );					// close the socket if we recieve zero
					close( sockConnect );
					exit( 1 );
				}

				inputBytes += bytes;
				if( inputBytes == fileSize ) 	break;
			}
			inputBytes += bytes;
			totalBytes += bytes;

			if( inputBytes >= BUFFER )                              // if our buffer is full
			{
				sendLine[ BUFFER ] = '\0';
				mhash( myHash, sendLine, BUFFER );              // update the hash with the buffer contents
				inputBytes = 0;
			}
		}

		if( inputBytes > 0 )                                            // if there are still bytes in the buffer to write
		{
			//printf( "Hashing... %s\n", sendLine );
			sendLine[ inputBytes ] = '\0';
			mhash( myHash, sendLine, inputBytes );                  // update the hash with the buffer contents
		}		

		fclose( filePtr );
		computeHash = mhash_end( myHash );				// done computing the hash

		/*
		printf( "Our hash: " );

		for( i = 0; i < HASH_SIZE; i++ )
		{
			printf( "%02x", computeHash[i] );
		}
		*/

		if( ( outputBytes = write( sockConnect, computeHash, HASH_SIZE ) ) <= 0 )
        	{
			perror( "Client - write() error" );
			exit( 1 );
        	}
	
		FILE *filePtrSend = fopen( filename, "r" );			// prepare to read the file again

                inputBytes = 0;                                                 // inputBytes is the number of bytes in the current buffer
                totalBytes = 0;

		while( totalBytes < fileSize )					// first runthrough of the file computes hash
		{
			if( fileSize > BUFFER ) 						// fill the rest of the buffer if our file is too big for a single
			{
				if( ( bytes = fread( sendLine + inputBytes, sizeof( char ), BUFFER - inputBytes, filePtrSend ) ) <= 0 )	// transfer just enough to fill the buffer
				{
					perror( "Error reading from file" );					// close the socket if we recieve zero
					close( sockConnect );
					exit( 1 );
				}
			}
			else                                                                            // if our file will not overflow the buffer
			{
				if( ( bytes = fread( sendLine + inputBytes, sizeof( char ), fileSize - inputBytes, filePtrSend ) ) <= 0 )	// transer the entire file into the buffer
				{
					perror( "Error reading from file" );					// close the socket if we recieve zero
					close( sockConnect );
					exit( 1 );
				}

				inputBytes += bytes;
				if( inputBytes == fileSize )	break;
			}
			inputBytes += bytes;
			totalBytes += bytes;

			if( inputBytes >= BUFFER )                              // if our buffer is full
			{
				sendLine[ BUFFER ] = '\0';
				if( ( outputBytes = write( sockConnect, sendLine, BUFFER ) ) <= 0 )
        			{
					perror( "Server - write() error" );
					exit( 1 );
        			}
				inputBytes = 0;
			}
		}

		if( inputBytes > 0 )                                            // if there are still bytes in the buffer to write
		{
			sendLine[ inputBytes ] = '\0';
			if( ( outputBytes = write( sockConnect, sendLine, inputBytes ) ) <= 0 )
        		{
				perror( "Server - write() error" );
				exit( 1 );
        		}	
		}

		if( close( sockConnect ) != 0 )					// close the socket
		{
			printf( "Server - sockConnect closing failed!\n" );
		}		
	}

	if( close( sockListen ) != 0 )					// close the socket
	{
		printf( "Server - sockfd closing failed!\n" );
	}		

	return 0;
}

