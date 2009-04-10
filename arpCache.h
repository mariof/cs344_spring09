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

void insert(arpNode **head, uint32_t ip, uint8_t *mac);
void deleteIP(arpNode **head, uint32_t ip);
void deleteMAC(arpNode **head, uint8_t *mac);
void timeout(arpNode **head);

arpTreeNode* generateTree(arpNode *head);
uint8_t* lookupTree(arpTreeNode *root, uint32_t ip);
void destroyTree(arpTreeNode *root);

pthread_mutex_t list_lock;
pthread_rwlock_t tree_lock;

