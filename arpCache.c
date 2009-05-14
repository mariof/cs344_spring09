#include "router.h"

/* 
ARP cache is kept as a sorted linked list, however since many more lookups into the cache are expected than cache modifications, a binary search tree is generated from the list to provide faster lookups.
*/

// insert new node into the list, mac is borrowed
void arpInsert(arpNode **head, uint32_t ip, uint8_t *mac, int is_static){
	int i;
	arpNode *n = (arpNode*)malloc(sizeof(arpNode));	
	n->ip = ip;
	for(i = 0; i < 6; i++) n->mac[i] = mac[i];
	n->t = time(NULL);
	n->is_static = is_static;
	n->next = n->prev = NULL;

	// find existing entry
	arpNode *tmp = arpFindIP(*head, ip);
	if(tmp){
	 	if (is_static || (!(tmp->is_static)) ){
 			arpDeleteIP(head, ip);
 		}
		else{
			free(n);
			return;
		}
	}
	
	// add new entry
	pthread_mutex_lock(&list_lock);

	if (*head == NULL){
		*head = n;
		pthread_mutex_unlock(&list_lock);
		return;
	}

	arpNode *cur = *head;
	arpNode *last = NULL;
	while(cur){
		if (ip < cur->ip)
			break;
		last = cur; 
		cur = cur->next;
	}
		
	if (cur){
		n->prev = cur->prev;
		n->next = cur;
		if(cur->prev) 
			cur->prev->next = n;
		else
			*head = n;
		cur->prev = n;		
	}
	else{
		last->next = n;
		n->prev = last;
	}
	
	pthread_mutex_unlock(&list_lock);

}

// returns list node given IP
arpNode* arpFindIP(arpNode *head, uint32_t ip){
	pthread_mutex_lock(&list_lock);

	if (head == NULL){
		pthread_mutex_unlock(&list_lock);
		return NULL;
	}
	
	arpNode *cur = head;
	while(cur){
		if(cur->ip == ip){
			break;
		}	
		cur = cur->next;
	}
	pthread_mutex_unlock(&list_lock);

	return cur;
}

// delete a list entry given an IP
void arpDeleteIP(arpNode **head, uint32_t ip){
	pthread_mutex_lock(&list_lock);

	if (*head == NULL){
		pthread_mutex_unlock(&list_lock);
		return;
	}
	
	arpNode *cur = *head;
	while(cur){
		if(cur->ip == ip){
			if(cur->prev){
				cur->prev->next = cur->next;
			}
			else{
				*head = cur->next;
			}
			if(cur->next){
				cur->next->prev = cur->prev;
			}			
			free(cur);
			break;
		}	
		cur = cur->next;
	}
	pthread_mutex_unlock(&list_lock);

}

// delete a list entry given a MAC
void arpDeleteMAC(arpNode **head, uint8_t *mac){
	int i;
	
	pthread_mutex_lock(&list_lock);

	if (*head == NULL){
		pthread_mutex_unlock(&list_lock);
		return;
	}
	
	arpNode *cur = *head;
	while(cur){
		int eq = 1;
		for(i = 0; i < 6; i++) if(cur->mac[i] != mac[i]) eq = 0;
				
		if(eq){
			if(cur->prev){
				cur->prev->next = cur->next;
			}
			else{
				*head = cur->next;
			}
			if(cur->next){
				cur->next->prev = cur->prev;
			}			
			free(cur);
			break;
		}	
		cur = cur->next;
	}
	
	pthread_mutex_unlock(&list_lock);

}

// delete old entries in the list, return 0 if nothing was done, 1 if list was updated
int arpTimeout(arpNode **head){
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	pthread_mutex_lock(&list_lock);
	int retVal = 0;
	
	if (*head == NULL){
		pthread_mutex_unlock(&list_lock);
		return 0;
	}
	arpNode *cur = *head;
	while(cur){
		if( (!cur->is_static) && ( ( time(NULL) - cur->t ) > ARP_CACHE_TIMEOUT ) ){
			retVal = 1;
			if(cur->prev){
				cur->prev->next = cur->next;
			}
			else{
				*head = cur->next;
			}
			if(cur->next){
				cur->next->prev = cur->prev;
			}			
			arpNode *tmp = cur->prev;
			free(cur);
			
			if(tmp)
				cur = tmp;
			else
				cur = *head;
							
		}
		else if((!cur->is_static) && ( ( time(NULL) - cur->t ) > ARP_CACHE_TIMEOUT-1 )){ // preemptive arp request
			char *out_if = lp_match(&(subsystem->rtable), cur->ip); //output interface
			if(out_if) sendARPrequest(sr, out_if, cur->ip);
			free(out_if);	
		}	
		if(cur) cur = cur->next;
	}

	pthread_mutex_unlock(&list_lock);
	return retVal;
}

arpTreeNode* createTree(arpNode *head){
	int i;
	if (head == NULL)
		return NULL;
		
	arpNode *tmp = head;
	arpNode *mid = head;
	
	while(tmp){
		tmp = tmp->next;
		if(tmp) 
			tmp = tmp->next;
		else
			break;
		mid = mid->next;
	}		

	arpTreeNode *tree = (arpTreeNode*)malloc(sizeof(arpTreeNode));
	tree->ip = mid->ip;
	for(i = 0; i < 6; i++) tree->mac[i] = mid->mac[i];

	if(mid->prev) mid->prev->next = NULL;
	if(mid->next) mid->next->prev = NULL;
	
	if(mid->prev)
		tree->left = createTree(head);
	else
		tree->left = NULL;
		
	tree->right = createTree(mid->next);
	
	if(mid->prev) mid->prev->next = mid;
	if(mid->next) mid->next->prev = mid;
	
	return tree;	
}

// generate binary search tree (balanced) from the sorted list
arpTreeNode* arpGenerateTree(arpNode *head){
	arpTreeNode *rv;
	pthread_mutex_lock(&list_lock);
	rv = createTree(head);
	pthread_mutex_unlock(&list_lock);
	return rv;
}

// do lookup into the tree O(logn), destroy return value when done with it
uint8_t* arpLookupTree(arpTreeNode *root, uint32_t ip){
	uint8_t *rv;
	
	pthread_rwlock_rdlock(&tree_lock);
	
	if (root == NULL){
		pthread_rwlock_unlock(&tree_lock);
		return NULL;
	}
		
	if(ip < root->ip){
		rv = arpLookupTree(root->left, ip);
		pthread_rwlock_unlock(&tree_lock);
		return rv;
	}
	else if(ip > root->ip){
		rv = arpLookupTree(root->right, ip);
		pthread_rwlock_unlock(&tree_lock);
		return rv;
	}
	else if (ip == root->ip){
		uint8_t *retVal = (uint8_t*)malloc(6*sizeof(uint8_t));
		int i;
		for(i = 0; i < 6; i++) retVal[i] = root->mac[i];
		pthread_rwlock_unlock(&tree_lock);
		return retVal;
	}
	pthread_rwlock_unlock(&tree_lock);
	return NULL;
}

// call arpReplaceTree with NULL newTree to destroy it
void arpDestroyTree(arpTreeNode *root){
	if(root == NULL) return;
	if(root->left) arpDestroyTree(root->left);
	if(root->right) arpDestroyTree(root->right);
	free(root);
}

// compares trees, caller should hold any necessary locks on trees
// returns 1 if trees are the same, 0 otherwise
int arpCompareTrees(arpTreeNode *treeA, arpTreeNode *treeB){
	int i;

	if(treeA == NULL && treeB == NULL) return 1;
	if(treeA == NULL || treeB == NULL) return 0;
	
	for(i = 0; i < 6; i++) if(treeA->mac[i] != treeB->mac[i]) return 0;
	if(treeA->ip != treeB->ip) return 0;

	return arpCompareTrees(treeA->left, treeB->left) && arpCompareTrees(treeA->right, treeB->right);
}

// replaces old tree with a new one in a thread safe fashion
// replace with NULL to destroy tree
void arpReplaceTree(arpTreeNode **root, arpTreeNode *newTree){
	pthread_rwlock_wrlock(&tree_lock);
	arpTreeNode *oldTree = *root;
	*root = newTree;
	pthread_rwlock_unlock(&tree_lock);

#ifdef _CPUMODE_
	pthread_rwlock_rdlock(&tree_lock);
	if(!arpCompareTrees(oldTree, newTree)){
		int index = 0;
		int i;
		
		pthread_rwlock_rdlock(&tree_lock);
			writeARPCache(newTree, &index);
		pthread_rwlock_unlock(&tree_lock);

		pthread_mutex_lock(&arpRegLock);
		for(i = index; i < ROUTER_OP_LUT_ARP_TABLE_DEPTH; i++){
			writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_ENTRY_NEXT_HOP_IP_REG, 0 );
			writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_ENTRY_MAC_HI_REG, 0 );
			writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_ENTRY_MAC_LO_REG, 0 );
			writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_WR_ADDR_REG, index++ );	
		}
		pthread_mutex_unlock(&arpRegLock);
	}
	pthread_rwlock_unlock(&tree_lock);
#endif // _CPUMODE_

	if(oldTree) arpDestroyTree(oldTree);

}


/**
 * ---------------------------------------------------------------------------
 * -------------------- CLI Functions ----------------------------------------
 * ---------------------------------------------------------------------------
 */
 
 
 /**
 * Add a static entry to the static ARP cache.
 * @return 1 if succeeded (fails if the max # of static entries are already
 *         in the cache).
 */
int arp_cache_static_entry_add( struct sr_instance* sr,
                                uint32_t ip,
                                uint8_t* mac ) {
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	arpInsert(&subsystem->arpList, ip, mac, 1);
    arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));

    return 1; /* succeede */
}

/**
 * Remove a static entry to the static ARP cache.
 * @return 1 if succeeded (false if ip wasn't in the cache as a static entry)
 */
int arp_cache_static_entry_remove( struct sr_instance* sr, uint32_t ip ) {
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	arpNode *node = arpFindIP(subsystem->arpList, ip);
	if(node->is_static == 1){
		arpDeleteIP(&subsystem->arpList, ip);		
		arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));
		return 1;
	}

    return 0; /* fail */
}

/**
 * Remove all static entries from the ARP cache.
 * @return  number of static entries removed
 */
unsigned arp_cache_static_purge( struct sr_instance* sr ) {
	int cnt = 0;
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	
	pthread_mutex_lock(&list_lock);

	if (subsystem->arpList != NULL){	
		arpNode *cur = subsystem->arpList;
		while(cur){
			arpNode* tmp = cur->next;
			if(cur->is_static == 1){
				if(cur->prev){
					cur->prev->next = cur->next;
				}
				else{
					subsystem->arpList = cur->next;
				}
				if(cur->next){
					cur->next->prev = cur->prev;
				}			
				free(cur);			
				cnt++;
			}	
			cur = tmp;
		}
	}
	pthread_mutex_unlock(&list_lock);
	
	arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));

    return cnt;
}

/**
 * Remove all dynamic entries from the ARP cache.
 * @return  number of dynamic entries removed
 */
unsigned arp_cache_dynamic_purge( struct sr_instance* sr ) {
	int cnt = 0;
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	
	pthread_mutex_lock(&list_lock);

	if (subsystem->arpList != NULL){	
		arpNode *cur = subsystem->arpList;
		while(cur){
			arpNode* tmp = cur->next;
			if(cur->is_static == 0){
				if(cur->prev){
					cur->prev->next = cur->next;
				}
				else{
					subsystem->arpList = cur->next;
				}
				if(cur->next){
					cur->next->prev = cur->prev;
				}			
				free(cur);			
				cnt++;
			}	
			cur = tmp;
		}
	}
	pthread_mutex_unlock(&list_lock);
	
	arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));

    return cnt;
}
