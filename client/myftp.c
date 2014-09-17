
/*

Nicholas LaRosa
CSE 30264, Project 1, Client

usage: myftp <IP_Address> OR <Host_Name> <Port_Number> <File_to_Send>

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
	int sockfd, i, err, isHost;
	int inputBytes, bytes, outputBytes, totalBytes;
	struct sockaddr_in serverAddress;
	struct addrinfo *hostInfo, *p;

	char sendLine[ BUFFER + 1 ];
	char recvLine[ BUFFER + 1 ];
	char ipstr[INET6_ADDRSTRLEN];

	char * filename;
	char * outputString;

	uint16_t filenameSize;		 	// client sends the size of the requested file name
	uint32_t fileSize;			// client recieves the size of the requested file
	unsigned int length;			// length of bytes written to a file	

	struct timeval startTimer;		// keep structs for recording timestamp at start and end of transfer
	struct timeval endTimer;

	int finalSec;
	int finalMilli;
	long int totalMilli;			// total number of milliseconds
	float speedInMB;			// we will calculate the file transfer speed in MB per second

	MHASH myHash;					// prepare for the md5 hashing of file retrieved
	unsigned char readHash[ HASH_SIZE + 1 ];	
	unsigned char * computeHash;			// have separate character arrays for hash retrieved and hash computed after retrieval
	unsigned char hashBuffer;	

	if( argc != 4 )
	{
		printf("\nusage: myftp <IP_Address> OR <Host_Address> <Port_Number> <File_to_Request>\n\n");
		exit( 1 );
	}
	
	if( ( sockfd = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
	{
		perror( "Client - socket() error" );
		exit( 1 );
	}

	memset( ( char * )&serverAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons( atoi( argv[2] ) );			// and receive server port

	isHost = 0;								// check for host name or IP address

	for( i = 0; i < strlen( argv[1] ); i++ )
	{
		if( argv[1][i] > 64 )
		{
			isHost = 1;
			break;
		}
	}
	
	if( isHost )
	{
   		if( ( err = getaddrinfo( argv[1], NULL, NULL, &hostInfo ) ) < 0 ) {
			printf("getaddrinfo() error %d\n", err);
			return 1;
		}
	
		// Source: Beej's Guide to Network Programming
		for( p = hostInfo; p != NULL; p = p->ai_next ) 
		{
			void *addr;

			if( p->ai_family == AF_INET )		// IPv4 
			{
				struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
				addr = &(ipv4->sin_addr);
			}
			else 					// IPv6
			{
				struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
				addr = &(ipv6->sin6_addr);
			}
			
			// convert the IP to a string and set it as our server
			inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);		
			serverAddress.sin_addr.s_addr = inet_addr( ipstr );
		}
		
		freeaddrinfo( hostInfo );		// free the linked list
	}
	else
	{
		serverAddress.sin_addr.s_addr = inet_addr( argv[1] );					// receive server address
	}	

	if( connect( sockfd, (struct sockaddr *)&serverAddress, sizeof( struct sockaddr_in ) ) < 0 )	// connect() via TCP
	{
		perror( "Client - connect() error" );
		exit( 1 );
	}

	filenameSize = strlen( argv[3] );
	//printf( "Filename size: %d\n", filenameSize );

	filenameSize = htons( filenameSize );			// host to network short

	memcpy( sendLine, &filenameSize, sizeof( uint16_t ) );	// store size in char array

	filename = &( sendLine[ sizeof( uint16_t ) ] );		// filename is contained after the 16-bit size

	strcpy( filename, argv[3] );				// add filename to sendLine

	//printf( "Sending: %s\n", &sendLine[2] );

	if( ( outputBytes = sendto( sockfd, sendLine, sizeof( uint16_t ) + strlen( &sendLine[ sizeof( uint16_t ) ] ), 0, ( struct sockaddr * )&serverAddress, sizeof( struct sockaddr_in ) ) ) < 0 )
	{
		perror( "Client - sendto() error" );
		exit( 1 );
	}

	read( sockfd, ( char * )&fileSize, sizeof( uint32_t ) );			// write the received character array to our 32-bit int, holding file size

	fileSize = ntohl( fileSize );							// convert to the client's endianness (network to host long)

	//printf( "The requested file has size %d\n", fileSize );

	if( ( bytes = read( sockfd, readHash, HASH_SIZE ) ) <= 0 )			// read in the 16-byte hash
	{
		perror( "Client - read() error" );					// close the socket if we recieve zero bytes
		close( sockfd );
		exit( 1 );
	}

	readHash[ HASH_SIZE ] = '\0';					// retrieve the hash

	/*
	printf( "The server sends: " );

	for( i = 0; i < HASH_SIZE; i++ )
	{	
		printf( "%02x", readHash[i] );
	}
	*/

	FILE *filePtr = fopen( filename, "w" );				// prepare to read and write to file
	if( filePtr == NULL )
	{
		perror( "File cannot be retrieved" );
		exit( 1 );
	}

	myHash = mhash_init( MHASH_MD5 );				// prepare to perform the MD5 hashing function
	if( myHash == MHASH_FAILED )
	{
		perror( "Client - mhash() error" );
		exit( 1 );
	}

	inputBytes = 0;							// inputBytes is the number of bytes in the current buffer
	totalBytes = 0;							// and totalBytes is the number of bytes currently written to our file

	gettimeofday( &startTimer, NULL );				// get the time at the start of the file transfer
	//printf( "Start Timestamp: %d seconds, %d milliseconds\n", startTimer.tv_sec, startTimer.tv_usec );

	while( totalBytes < fileSize )
	{
		if( fileSize > BUFFER )								// fill the rest of the buffer if our file is too big for a single
		{
			if( ( bytes = read( sockfd, sendLine + inputBytes, BUFFER - inputBytes ) ) <= 0 )	// transfer just enough to fill the buffer
			{
				perror( "Client - read() error" );						// close the socket if we recieve zero
				close( sockfd );
				exit( 1 );
			}
		}
		else										// if our file will not overflow the buffer
		{
			if( ( bytes = read( sockfd, sendLine + inputBytes, fileSize - inputBytes ) ) <= 0 )	// transer the entire file into the buffer
			{
				perror( "Client - read() error" );					// close the socket if we recieve zero
				close( sockfd );
				exit( 1 );
			}

			inputBytes += bytes;
			if( inputBytes == fileSize )	break;
		}
		inputBytes += bytes;
		totalBytes += bytes;		

		if( inputBytes >= BUFFER )				// if our buffer is full
		{
			sendLine[ BUFFER ] = '\0';
			length = fwrite( sendLine, sizeof( char ), BUFFER, filePtr );
			if( length == 0 )
			{
				perror( "Error writing to file" );
				exit( 1 );
			}

			mhash( myHash, sendLine, BUFFER );		// update the hash with the buffer contents
			inputBytes = 0;
		}
	}

	if( inputBytes > 0 )						// if there are still bytes in the buffer to write 
	{
		sendLine[ inputBytes ] = '\0';
		length = fwrite( sendLine, sizeof( char ), inputBytes, filePtr );
		if( length == 0 )
		{
			perror( "Error writing to file" );
			exit( 1 );
		}

		mhash( myHash, sendLine, inputBytes );			// update the hash with the buffer contents
	}

	gettimeofday( &endTimer, NULL );				// get the time at the start of the file transfer
	//printf( "End Timestamp: %d seconds, %d milliseconds\n", endTimer.tv_sec, endTimer.tv_usec );

	computeHash = mhash_end( myHash );				// end the hashing function

	//printf( "Recieved file's hash: " );

	for( i = 0; i < HASH_SIZE; i++ ) 
	{
		//printf( "%02x", computeHash[i] );
		if( readHash[i] != computeHash[i] )
		{
			printf( "\nThe file transfer was corrupted. Try again!\n\n" );
			exit( 1 );
		}	
	}

	fclose( filePtr );					// close the file pointer

	if( endTimer.tv_usec >= startTimer.tv_usec )			// calculate the second and millisecond difference
	{
		finalSec = endTimer.tv_sec - startTimer.tv_sec;
		finalMilli = endTimer.tv_usec - startTimer.tv_usec;
	}
	else
	{
		finalSec = endTimer.tv_sec - startTimer.tv_sec - 1;
		finalMilli = endTimer.tv_usec + ( 1000000 - startTimer.tv_usec );
	}

	totalMilli = finalMilli + 1000*finalSec;
	speedInMB = ( (float)fileSize / totalMilli * 1000000 / 1024 / 1024 );	// there are 1024 bytes/kilobyte and 1024 kilobytes/megabyte	

	printf( "\n%d bytes transferred in %02d.%06d seconds : %02.04f Megabytes/sec\n", fileSize, finalSec, finalMilli, speedInMB );
	printf( "File MD5sum: " );
	for( i = 0; i < HASH_SIZE; i++ ) 
	{
		printf( "%02x", computeHash[i] );	
	}

	printf( "\n\n" );

	if( close( sockfd ) != 0 )					// close the socket
	{
		printf( "Client - sockfd closing failed!\n" );
	}		

	return 0;
}

