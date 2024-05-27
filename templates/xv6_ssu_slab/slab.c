#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "slab.h"

struct {
	struct spinlock lock;
	struct slab slab[NSLAB];
} stable;

void set_bit(char *bitmap, int index, int value) {
    int byteIndex = index / 8;
    int bitOffset = index % 8;
    if (value)
        bitmap[byteIndex] |= (1 << bitOffset);
    else
        bitmap[byteIndex] &= ~(1 << bitOffset);
}

int get_bit(char* bitmap, int index) {
    int byteIndex = index / 8;
    int bitOffset = index % 8;
    return (bitmap[byteIndex] >> bitOffset) & 1;
}

void slabinit(){
	int i = 0;
	struct slab *s;

	acquire(&stable.lock);
	for(i = 0; i < NSLAB; i++) {
		s = &stable.slab[i];

		s->size = 1 << (i+4);
		s->bitmap = kalloc();
		memset(s->bitmap, 0, PGSIZE);
		memset(s->page, 0, MAX_PAGES_PER_SLAB);

		s->page[0] = kalloc();
		s->num_pages++;
		s->num_objects_per_page = PGSIZE / s->size;
		s->num_free_objects = s->num_objects_per_page;
		s->num_used_objects = 0;
	}

	release(&stable.lock);
}

char *kmalloc(int size){
	struct slab *s;
	char *bitmap;
	int num_objects_per_page, page_idx;
	char offset;

	acquire(&stable.lock);
	for(s = stable.slab; s < &stable.slab[NSLAB]; s++) {
		if (size <= s->size)
			break;
	}

	bitmap = s->bitmap;
	num_objects_per_page = s->num_objects_per_page;

	// empty, partial, full
	// find partial page
    for (int i = 0; i < num_objects_per_page * MAX_PAGES_PER_SLAB; i++) {
        if (!get_bit(bitmap, i)) { 
			page_idx = i / num_objects_per_page;
			offset = i % num_objects_per_page;

			// is page allocated? 0 == no
			if (s->page[page_idx]) {
				set_bit(bitmap, i, 1); 
				s->num_used_objects++;
				s->num_free_objects--;
				release(&stable.lock);
				return s->page[page_idx] + offset * s->size;
			}
        }
    }

	// if there is no partial page, find empty page 
    for (int i = 0; i < num_objects_per_page * MAX_PAGES_PER_SLAB; i++) {
        if (!get_bit(bitmap, i)) { 
			page_idx = i / num_objects_per_page;
			offset = i % num_objects_per_page;

			s->page[page_idx] = kalloc();
			set_bit(bitmap, i, 1); 

			s->num_used_objects++;
			s->num_free_objects += s->num_objects_per_page - 1;
			s->num_pages++;
			release(&stable.lock);
			return s->page[page_idx] + offset * s->size;
        }
    }

	release(&stable.lock);
    return 0; // impossible to allocate

}

void kmfree(char *addr, int size){
	int index, i;
	int offset = 0;
	char isfree = 1;
	char *bitmap;
	struct slab *s;

    if (!addr || size <= 0) {
        return;
    }

	// find slab
	acquire(&stable.lock);
	for(s = stable.slab; s < &stable.slab[NSLAB]; s++) {
		if (size <= s->size)
			break;
	}
	
	for (i = 0; i < MAX_PAGES_PER_SLAB; i++) {
		if (!s->page[i])
			continue;
		offset = addr - s->page[i];
		if (0 <= offset && offset < PGSIZE){
			break;
		}
	}

	// if (offset < 0 || PGSIZE <= offset)
	// 	cprintf("Error: offset(%d - %d = %d) is invalid\n", addr, s->page[i], offset);

	bitmap = s->bitmap;	
	index = i * s->num_objects_per_page + offset / s->size;
    if(!get_bit(bitmap, index)) {
		release(&stable.lock);
		return; 
	} 

	set_bit(bitmap, index, 0);
	s->num_free_objects++;
	s->num_used_objects--;

	// validate page is free
    for (int j = i * s->num_objects_per_page; j < (i+1) * s->num_objects_per_page; j++) {
        if (get_bit(bitmap, j)) {
			isfree = 0;
			break;
        }
    }

	// page deallocation
	if (isfree) {
		kfree(s->page[i]);
		s->page[i] = 0;
		s->num_pages--;
		s->num_free_objects -= s->num_objects_per_page;
	}

	// slabdump();
	release(&stable.lock);
}

/* Helper functions */
void slabdump(){
	cprintf("__slabdump__\n");

	struct slab *s;

	cprintf("size\tnum_pages\tused_objects\tfree_objects\n");

	for(s = stable.slab; s < &stable.slab[NSLAB]; s++){
		cprintf("%d\t%d\t\t%d\t\t%d\n", 
			s->size, s->num_pages, s->num_used_objects, s->num_free_objects);
	}
}

int numobj_slab(int slabid)
{
	return stable.slab[slabid].num_used_objects;
}

int numpage_slab(int slabid)
{
	return stable.slab[slabid].num_pages;
}
