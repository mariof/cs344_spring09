#include "arpCache.h"

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
		cur = cur->next;
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

// replaces old tree with a new one in a thread safe fashion
// replace with NULL to destroy tree
void arpReplaceTree(arpTreeNode **root, arpTreeNode *newTree){
	arpTreeNode *oldTree = *root;
	pthread_rwlock_wrlock(&tree_lock);
	*root = newTree;
	pthread_rwlock_unlock(&tree_lock);
	if(oldTree) arpDestroyTree(oldTree);
}