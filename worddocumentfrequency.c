#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <dirent.h>
#include <omp.h>
#include <time.h>
//#define _DEBUG_
#define WARN(msg, ...) printf("LINE: %d " msg "\n", __LINE__, ##__VA_ARGS__);

#ifndef TOPWORDCOUNT
	#define TOPWORDCOUNT 10
#endif
#ifndef NUMCORES
	#define NUMCORES 4
#endif
#ifndef MAXTRIECOUNT
	#define MAXTRIECOUNT 1024
#endif
#ifndef BUFFERSIZE
	#define BUFFERSIZE BUFSIZ
#endif
#ifndef MAXWORDSIZE
	#define MAXWORDSIZE 50
#endif

#pragma pack(1)
struct _trie {
	uint32_t count[26];
	bool counted[26][NUMCORES];
	struct _trie *next[26]; 
	omp_lock_t lock[26];
};
#pragma pack()

typedef struct _trie trie ;
typedef struct _trie *Trie ;

// This ensures that values are set before some other node tries to access it...
// Idea: DOn't Allocate such 
// Note: <---------------------- This one prevents SIGSEGV...
#define NEWNODE(node) ({\
	Trie tmp = malloc(sizeof(trie));\
	for (int i=0; i< 26; i++) {\
	 	tmp->count[i] =0;\
	 	tmp->next[i] =NULL;\
	 	omp_init_lock(&(tmp->lock[i]));\
	 	for(int j=0; j< NUMCORES; j++)\
	 		tmp->counted[i][j] =false;\
	}\
	node = tmp;\
})

int _printtrie ( Trie root, char string[], int pos) {
	if ( root == NULL) { WARN("Print trie called with NULL trie."); return 0; }
	int count = 0;
	for ( int i =0; i< 26; i++ ) {
		if ( root->count[i] >0 ) 
			//string[pos] =0, printf("%*s%s%c:%d\n",pos,"",string, i+'A', root->count[i]);
			string[pos] =0, printf("%s%c:%d ",string, i+'A', root->count[i]);
		if ( root->next[i] != NULL) {
			string[pos] =i +'A';
			count += _printtrie(root->next[i],string, pos+1 ); // get number of nodes in children
		}
	}
	return count + 1;// 1 for current node ..
}

void printtrie( Trie root) {
	char string[MAXWORDSIZE+1];
	int count = _printtrie(root, string, 0);
	printf("\n TOTAL TRIE BLOCK COUNT: %d\n",count );
}

void cleartrie ( Trie root, int threadid ) {
	if ( root == NULL) { 
		WARN("\033[031mClear trie called with NULL trie.\033[0m\n"); return; 
	}
	for ( int i =0; i< 26; i++ ) {
		root->counted[i][threadid] = false;
		if ( root->next[i] != NULL) cleartrie(root->next[i], threadid); 
	}
}

void readfile(char filename[], Trie root, int threadid) {
	#define LOAD(buffer) buffer[fread(buffer,sizeof(char),BUFFERSIZE,fp)]= EOF;
	#define ENDOFBUFFER(buffer) buffer+BUFFERSIZE
	if ( root == NULL ) { 
		WARN("Insert called with NULL trie.");
		return;
	}
	#define TPRINTF(FORMAT, ...) printf("READFILE %s [NUMCORES:%d] [TID:%d] " FORMAT, filename, NUMCORES, threadid, ##__VA_ARGS__)
	// TPRINTF("\033[033m%s\033[0m ",filename);
	FILE *fp;
	char buffer1[BUFFERSIZE+1], buffer2[BUFFERSIZE+1];
	char *forward =buffer1, curchar;
	int index, previndex, wordsize =0;
	Trie node = root;
	#ifdef _DEBUG_
		char string[MAXWORDSIZE+1]; 
	#endif
	fp = fopen(filename, "r");
	if ( fp == NULL ) {
		TPRINTF("Unable to open file '%s'.\n", filename);
		perror("");
		return;
	}
	LOAD(buffer1);
	while ( true ) {
		if ( (*forward) == EOF ) { // current value of forward pointer is EOF, If end of buffers load the buffer 
			if ( forward == ENDOFBUFFER(buffer1) ){ 
				LOAD(buffer2); // load the buffer
				forward = buffer2 ; // make forward point to it
			}
			else if (forward == ENDOFBUFFER(buffer2)) {
				LOAD(buffer1);
				forward = buffer1 ;
			}
			else break; // No more characters that is end of file
		}
		else {
			curchar = *forward ;
			forward ++;
			switch ( curchar ){
				case 'a'...'z':
					curchar = curchar - 32;
				case 'A'...'Z':
					index = curchar - 'A';
					#ifdef _DEBUG_
						string[wordsize] = curchar ;
					#endif
					if (wordsize == 0 ) { 		// No creating new node if first string ...
						previndex = index ;
						wordsize ++ ;
						break ;
					}
					else {
						if ( node->next[previndex] == NULL ){
							omp_set_lock(&(node->lock[previndex]));
							// printf("[TID: %d] Obtained LOCK.\n",threadid);
							if ( node->next[previndex] == NULL ) {
									NEWNODE( (node->next[previndex]) );
							}
							omp_unset_lock(&(node->lock[previndex]));	
						}
						node = node->next[previndex] ;
						previndex = index ;	
						wordsize ++;
						if ( wordsize < MAXWORDSIZE ) break;
	//					else TPRINTF("\033[034mMAXWORDSIZE reached thus forcing insertion.\033[0m. ");
					}
				default:
					if ( wordsize == 0 ) continue ; // Find other words..
					
					if ( !node->counted[index][threadid] ) {
						#ifdef _DEBUG_
							string[wordsize] = '\0' ;
							if ( strcmp(string,"A") == 0 ) printf("N: count%d before\n", node->count[index]);
							TPRINTF("Inserting Word '%s' wordsize: %d.\n", string, wordsize);
						#endif
						// #pragma omp atomic update
						 #pragma omp atomic
							node->count[index] ++;
						
						//	omp_set_lock(&(node->lock[index]));
						//	node->count[index] ++;
						//	omp_unset_lock(&(node->lock[index]));
						// Note: This is not shared so no need for automic operation..
						// Note: Now you need to insert to top 10 list 
						// Implement it later
						node->counted[index][threadid] =true; 

					}
					else {
						#ifdef _DEBUG_
							string[wordsize] = '\0' ;
							TPRINTF("Already Inserted Word '%s' wordsize: %d.\n", string, wordsize);
						#endif
					}
					node = root ;
					wordsize = 0;
			}
		}
	}
	fclose(fp);
	cleartrie(root, threadid);
}

//  END OF TRIE

#define STACKNODEBUFFERSIZE 1024
struct _stacknode 
{
	char path[PATH_MAX];
	struct _stacknode *next;
};

struct _stack
{
	int count;
	struct _stacknode *top;
};

typedef struct _stack stack;
typedef struct _stack *Stack;
typedef struct _stacknode stacknode ;
typedef struct _stacknode *Stacknode ;

#define push(stackname,filepath) ({\
	stackname->count++;\
	Stacknode tmp = stackname->top;\
	stackname->top = malloc(sizeof(stacknode));\
	stackname->top->next = tmp;\
	strcpy(stackname->top->path,filepath);\
})
#define pop(stackname,filepath) ({\
	if ( stackname->count == 0) printf("Stack empty");\
	else {\
		Stacknode tmpnode;\
		strcpy(filepath, stackname->top->path);\
		tmpnode = stackname->top->next;\
		free(stackname->top);\
		stackname->top = tmpnode;\
		stackname->count --;\
	}\
})

#define empty(stackname) ( stackname->count == 0 )
#define newstack(stackname) ({ stackname= malloc(sizeof(stack)); stackname->count=0; stackname->top=NULL; })

int listfiles_p(char root[], Trie rootnode)
{
	Stack files, dirs;
	newstack(files);
	newstack(dirs);
	push(dirs, root);
	int filecount= 0;
	#pragma omp parallel shared(files, dirs, filecount) num_threads(NUMCORES)
	{
		char filename[PATH_MAX+1], dirname[PATH_MAX+1], path[PATH_MAX+1];
		filename[PATH_MAX] =0; dirname[PATH_MAX]=0; path[PATH_MAX]=0;
		DIR *dir ;
		struct dirent *entry;
		bool foundfile = false, stacksempty= false ;
		int threadid = omp_get_thread_num();
		#undef TPRINTF
		#define TPRINTF(FORMAT, ...) printf("LISTFILE [TID:%d]" FORMAT, threadid, ##__VA_ARGS__)
		while ( !stacksempty ) {
			#pragma omp critical (FINDFILES)
			{
				filenotyetfound:
				foundfile = false ;
				if ( !empty(files) ) {
					// If files not empty
					pop(files, filename);
					// TPRINTF("FOUND FILE: %s\n",  filename);
					foundfile = true; // Now read that file..
					filecount ++;
				}
				else if ( !empty(dirs) ) {
					pop(dirs, dirname);
					dir = opendir(dirname);
					if ( dir == NULL ) {
						TPRINTF("\033[31m Unable to open dir '%s'.\033[0m", dirname); perror("");
						// What to do ..?
					}
					else {
						TPRINTF("\033[32mDIR: %s\033[0m ",dirname);
						while ( entry = readdir(dir) ) {
							switch( entry->d_type) {
								case DT_REG:
									snprintf(path, PATH_MAX, "%s/%s", dirname, entry->d_name);
									push(files, path); // Note: Pushing to stack not reading ...
									break;
								case DT_DIR:
									if ( strcmp(entry->d_name, ".") ==0 || strcmp(entry->d_name, "..") ==0 )
										continue ;
									snprintf(path, PATH_MAX, "%s/%s", dirname, entry->d_name);
									push(dirs, path); // Note: pushing to directory stack...
									break;
							}
						}
						closedir(dir);
						goto filenotyetfound; // change this later...
					}
				}
				else{
					stacksempty = true ;
				//	TPRINTF("\nNo More Files and Directories to read.\n");
				}
			}
			if ( foundfile ){
				// If any file remaining...
				TPRINTF("READING FILE: %s\n",  filename);
				readfile( filename, rootnode,	threadid);
				
			}
		}
	}
	//printf("FILE COUNT: %d\n",filecount);
	return filecount;
}

// If top words are less 
// this is sufficient ..
// else heap sort .. ( as the time this occupies is significantly lesser.. )
// <- TOP TEN
// <------------------- TOP WORDS
int numtopwords= 0;
struct _topwords {
	uint32_t count[TOPWORDCOUNT];
	char words[TOPWORDCOUNT][MAXWORDSIZE+1];
}; 
struct _topwords topwords ;
// Use these functions to insert at the end of trie being populated..
void insertword(char *word, int wordcount) {
	#ifdef _DEBUG_
		// printf("Insert Word: %s %d\n",word, wordcount);
	#endif
	// If top words count not full ...
	if ( numtopwords == 0 ) {
		// If no words 
		strcpy(topwords.words[0], word);
		topwords.count[0]= wordcount;
		numtopwords ++;
	}
	else if (numtopwords < TOPWORDCOUNT ){
		for ( int i= numtopwords; i> 0; i--) {
			if ( topwords.count[i-1] < wordcount ) {
				topwords.count[i] = topwords.count[i-1];
				strcpy(topwords.words[i], topwords.words[i-1]);
				if ( i == 1 ) {
					// If it goes to top ...
					topwords.count[0] = wordcount;
					strcpy(topwords.words[0], word);
				}
			}
			else {
				topwords.count[i] = wordcount;
				strcpy(topwords.words[i], word);
				break;
			}
		}
		numtopwords++;
	}
	// If top words is full..
	else {
		for ( int i= numtopwords; i >0; i--){
			if ( topwords.count[i-1] < wordcount ) {
				if ( i < TOPWORDCOUNT ){
					topwords.count[i] = topwords.count[i-1];
					strcpy(topwords.words[i], topwords.words[i-1]);
				} 
				// That is throw the last word out ..
				if ( i == 1 ) {
					// If it goes to top ...
					topwords.count[0] = wordcount;
					strcpy(topwords.words[0], word);
				}
			}
			else {
				if ( i < TOPWORDCOUNT ){
					topwords.count[i] = wordcount;
					strcpy(topwords.words[i], word);
				}
				// If smaller than last word, don't do anything
				break;
			}
		}
	}
}
// Do it in a single thread...
void _inserttopwords( Trie root, char string[], int pos) {
	if ( root == NULL ){ WARN("Print trie called with NULL trie."); return ; }
	for ( int i =0; i< 26; i++ ) {
		if ( root->count[i] >0 ) {
			string[pos]= i+'A';
			string[pos+1]=0;
			insertword(string, root->count[i]);
		}
		if ( root->next[i] != NULL) {
			string[pos] =i +'A';
			string[pos+1]=0;
			_inserttopwords(root->next[i], string, pos+1 ); 
		}
	}
}

void inserttopwords( Trie root ) {
	char string[MAXWORDSIZE+1];
	_inserttopwords(root, string, 0);
}

void printtopwords() {
	for ( int i= 0; i< numtopwords; i++) 
		printf("%d. %s %d\n",i+1, topwords.words[i],topwords.count[i]);
}
// END OF TOP TEN
int main(int argc, char *argv[]) {
	if ( argc != 2 ) {
		printf("\033[031mFormat:\033[0m executable directory\n");
		exit(EXIT_FAILURE);
	}
	// clock_t start = clock();
	double start = omp_get_wtime( ); 
	Trie root;
	NEWNODE(root);
	int filecount = listfiles_p(argv[1],root);
	double timetakendf =  (omp_get_wtime() -start );
	// printtrie(root);
	
	// Insert the words
	start = omp_get_wtime( );
	inserttopwords(root);
	double timetakentt =  ( omp_get_wtime( )- start );
	printtopwords();
	//printf("%d,%d,%lf,%lf\n",filecount, NUMCORES, timetakendf, timetakentt);
	printf("FILE COUNT: %d\n",filecount);
	printf("TIME TAKEN FOR Document Frequency computation.: %lf\n", timetakendf);
	printf("TIME TAKEN FOR Top Ten computation.: %lf\n", timetakentt);
	printf("NUMCORE: %d\n",NUMCORES );
	return 0;
}
