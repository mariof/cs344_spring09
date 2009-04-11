#include "sr_vns.h"
#include "sr_base_internal.h"
#include "sr_integration.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>

#define ARP_CACHE_TIMEOUT 300

struct arpCacheNode{
	uint32_t ip;
	uint8_t mac[6];
	time_t t;
	int is_static;
	struct arpCacheNode *next;
	struct arpCacheNode *prev;
};

struct arpCacheTreeNode{
	uint32_t ip;
	uint8_t mac[6];
	struct arpCacheTreeNode *left;
	struct arpCacheTreeNode *right;
};

typedef struct arpCacheNode arpNode;
typedef struct arpCacheTreeNode arpTreeNode;

void arpInsert(arpNode **head, uint32_t ip, uint8_t *mac, int is_static);
arpNode* arpFindIP(arpNode *head, uint32_t ip);
void arpDeleteIP(arpNode **head, uint32_t ip);
void arpDeleteMAC(arpNode **head, uint8_t *mac);
int arpTimeout(arpNode **head);

arpTreeNode* arpGenerateTree(arpNode *head);
uint8_t* arpLookupTree(arpTreeNode *root, uint32_t ip);
void arpReplaceTree(arpTreeNode **root, arpTreeNode *newTree);

pthread_mutex_t list_lock;
pthread_rwlock_t tree_lock;

