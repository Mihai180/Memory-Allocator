// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include "block_meta.h"

#define METADATA_SIZE sizeof(struct block_meta)  // Marimea stucturii pentru metadatele unui block
#define MMAP_THRESHOLD (128 * 1024)  // Marimea treshold-ului pentru folosirea mmap
#define HEAP_PREALLOC_SIZE (128 * 1024)  // Marimea memoriei prealocate pe heap

struct block_meta *head;

// Functie care uneste blocuri de memorie goale
void coalesce_blocks(void)
{
	struct block_meta *block = head;

	while (block && block->next) {
		if (block->status == STATUS_FREE && block->next->status == STATUS_FREE) {
			block->size += block->next->size + METADATA_SIZE;
			block->next = block->next->next;
			if (block->next)
				block->next->prev = block;
		} else {
			block = block->next;
		}
	}
}

void *os_malloc(size_t size)
{
	// Cazul in care marimea care trebuie alocata este 0
	if (size == 0)
		return NULL;

	// Aliniez marimea care trebuie alocata la 8 bytes
	size = (size + 7) & ~7;

	struct block_meta *block = head;
	struct block_meta *best_block = NULL;

	// Inainte de alocare unesc blocurile goale
	coalesce_blocks();

	// Gasesc cel mai potrivit bloc liber
	while (block != NULL) {
		if (block->status == STATUS_FREE && block->size >= size) {
			if (best_block == NULL || block->size < best_block->size)
				best_block = block;
	}
	block = block->next;
	}

	// Daca gasesc un bloc care se potriveste il aloc
	if (best_block != NULL) {
		if (best_block->size >= size + METADATA_SIZE + 8) {
			// Daca blocul este suficient de mare, il impart
			struct block_meta *new_block = (struct block_meta *)((char *)best_block + METADATA_SIZE + size);

			new_block->size = best_block->size - size - METADATA_SIZE;
			if (new_block->size >= 8) {
				new_block->status = STATUS_FREE;
				new_block->prev = best_block;
				new_block->next = best_block->next;

				if (new_block->next)
					new_block->next->prev = new_block;

				best_block->next = new_block;
				best_block->size = size;
			}
		}
		best_block->status = STATUS_ALLOC;  // Marchez nodul ca si alocat
		return (void *)((char *)best_block + METADATA_SIZE);  // Returnez pointer la memoria alocata
	}

	// Daca nu gasesc niciun block la pasul precedent incerc
	// sa maresc ultimul block daca acesta este gol
	struct block_meta *last_block = head;

	while (last_block && last_block->next != NULL)
		last_block = last_block->next;

	if (last_block && last_block->status == STATUS_FREE) {
		size_t additional_size = size - last_block->size;
		void *new_mem = sbrk(additional_size);

		DIE(new_mem == (void *)-1, "sbrk failed");
		last_block->size = last_block->size + additional_size;
		last_block->status = STATUS_ALLOC;
		return (void *)((char *)last_block + METADATA_SIZE);
	}

	// Daca nu s-a gasit niciun block si nu s-a putut mari
	// ultimul block, aloc un nou bloc de memorie
	void *new_mem = NULL;
	static int preallocated;

	// Daca marimea care trebuie alocata este mai mica decat
	// MMAP_TRESHOLD folosesc sbrk
	if (size < MMAP_THRESHOLD) {
		if (!preallocated) {
			// La prima alocare, aloc HEAP_PREALLOC_SIZE pe heap
			new_mem = sbrk(HEAP_PREALLOC_SIZE);
			DIE(new_mem == (void *)-1, "sbrk preallocation failed");
			if (new_mem == (void *)-1)
				return NULL;
			preallocated = 1;

			// Initializez metadatele block-ului de memorie prealocat
			struct block_meta *prealloc_block = (struct block_meta *)new_mem;

			prealloc_block->size = HEAP_PREALLOC_SIZE - METADATA_SIZE;
			prealloc_block->status = STATUS_FREE;
			prealloc_block->prev = NULL;
			prealloc_block->next = NULL;
			head = prealloc_block;

			// Dupa prealocare incerc din nou sa aloc o zona
			// de memorie de marimea size
			return os_malloc(size);
		}
		// Daca nu este prima alocare, aloc memorie cu sbrk
		new_mem = sbrk(size + METADATA_SIZE);
		DIE(new_mem == (void *)-1, "sbrk allocation failed");
		if (new_mem == (void *)-1)
			return NULL;
	} else if (size >= MMAP_THRESHOLD) {
		// Pentru size mai mare sau egal cu MMAP_TRESHOLD folosesc
		// mmap pentru alocare
		new_mem = mmap(NULL, size + METADATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		DIE(new_mem == MAP_FAILED, "mmap allocation failed");
		if (new_mem == MAP_FAILED)
			return NULL;
	}

	// Initializez metadatele noului block de memorie
	struct block_meta *new_block = (struct block_meta *)new_mem;

	new_block->size = size;
	if (size >= MMAP_THRESHOLD)
		new_block->status = STATUS_MAPPED;
	else
		new_block->status = STATUS_ALLOC;

	new_block->prev = NULL;
	new_block->next = NULL;

	// Adaug noul block de memorie la finalul listei de block-uri
	if (head == NULL) {
		head = new_block;
	} else {
		struct block_meta *current = head;

		while (current->next != NULL)
			current = current->next;
		current->next = new_block;
		new_block->prev = current;
	}

	// Returnez un pointer la zona de memorie a payload-ului
	return (void *)((char *)new_block + METADATA_SIZE);
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (nmemb == 0 || size == 0)
		return NULL;

	size_t total_size = nmemb * size;
	// Verific pentru overflow
	if (nmemb != 0 && total_size / nmemb != size)
		return NULL;

	// Aliniez marimea care trebuie alocata la 8 bytes
	total_size = (total_size + 7) & ~7;

	// Marimea unei pagini virtuale de memorie
	size_t page_size = 4080;

	// Pentru alocari mai mari decat marimea unei pagini
	// folosesc mmap
	if (total_size >= page_size) {
		void *new_mem = mmap(NULL, total_size + METADATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

		DIE(new_mem == MAP_FAILED, "mmap for calloc failed");
		if (new_mem == MAP_FAILED)
			return NULL;

		// Initializez metadatele block-ului de memorie
		struct block_meta *new_block = (struct block_meta *)new_mem;

		new_block->size = total_size;
		new_block->status = STATUS_MAPPED;
		new_block->prev = NULL;
		new_block->next = NULL;

		// Initializez toata zona de memorie alocata cu 0
		memset((char *)new_block + METADATA_SIZE, 0, total_size);
		return (void *)((char *)new_block + METADATA_SIZE);
	}

	// Pentru alocari mai mici decat marimea unei pagini
	// virtuale folosesc os_malloc
	void *new_mem = os_malloc(total_size);

	if (new_mem == NULL)
		return NULL;

	// Initializez toata zona de memorie alocata cu 0
	memset(new_mem, 0, total_size);
	return new_mem;
}

void *os_realloc(void *ptr, size_t size)
{
	if (ptr == NULL) {
		// Daca ptr este NULL are acelasi efect ca os_malloc(size)
		return os_malloc(size);
	}
	if (size == 0) {
		// Daca size = 0 are acelasi efect ca os_free(ptr)
		os_free(ptr);
		return NULL;
	}

	// Extrag block-ul de metadate din pointerul la zona de memorie
	struct block_meta *block = (struct block_meta *)((char *)ptr - METADATA_SIZE);

	if (block == NULL)
		return NULL;

	// Daca block-ul de memorie este gol nu pot sa il realoc
	if (block->status == STATUS_FREE)
		return NULL;

	// Aliniez marimea care trebuie alocata la 8 bytes
	size = (size + 7) & ~7;

	if (block->status == STATUS_MAPPED) {
		// Pentru blocuri alocate cu mmap
		if (size >= MMAP_THRESHOLD) {
			// Aloc un nou block cu mmap
			void *new_ptr = mmap(NULL, size + METADATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

			DIE(new_ptr == MAP_FAILED, "mmap for realloc failed");
			if (new_ptr == MAP_FAILED)
				return NULL;

			// Initializez metadatele pentru noul block de memorie
			struct block_meta *new_block = (struct block_meta *)new_ptr;

			new_block->size = size;
			new_block->status = STATUS_MAPPED;
			new_block->prev = NULL;
			new_block->next = NULL;

			// Copiez datele din vechiul block de memorie in cel nou
			size_t copy_size;

			if (block->size < size)
				copy_size = block->size;
			else
				copy_size = size;

			memcpy((char *)new_ptr + METADATA_SIZE, ptr, copy_size);

			// Eliberez vechiul block de memorie
			int unmap_status = munmap(block, block->size + METADATA_SIZE);

			DIE(unmap_status == -1, "munmap failed");
			if (unmap_status == -1) {
				munmap(new_ptr, size + METADATA_SIZE);
				return NULL;
			}

			return (char *)new_ptr + METADATA_SIZE;
		}
		// Pentru o marime noua mai mica decat MMAP_TRESHOLD aloc cu os_malloc
		void *new_ptr = os_malloc(size);

		if (new_ptr == NULL)
			return NULL;

		// Copiez datele din vechiul block de memorie in cel nou
		size_t copy_size;

		if (block->size < size)
			copy_size = block->size;
		else
			copy_size = size;

		memcpy(new_ptr, ptr, copy_size);

		// Eliberez vechiul block de memorie
		int unmap_status = munmap(block, block->size + METADATA_SIZE);

		DIE(unmap_status == -1, "munmap failed");
		if (unmap_status == -1) {
			os_free(new_ptr);
			return NULL;
		}
		return new_ptr;
	}

	if (size <= block->size) {
		// Daca marimea noua este mai mica decat marimea block-ului
		// incerc sa il impart
		if (block->size >= size + METADATA_SIZE + 8) {
			// Creez un nou block gol cu spatiul ramas
			struct block_meta *new_block = (struct block_meta *)((char *)block + METADATA_SIZE + size);

			new_block->size = block->size - size - METADATA_SIZE;
			new_block->status = STATUS_FREE;

			// Inserez noul block in lista
			new_block->prev = block;
			new_block->next = block->next;
			if (new_block->next)
				new_block->next->prev = new_block;
			block->next = new_block;
			block->size = size;
		}
		// Returnez pointer la aceeasi zona de memorie, dupa ce a fost prelucrata
		return ptr;
	}

	// Incerc sa maresc block-ul curent, unind-ul cu cele libere adiacente
	struct block_meta *current = block;

	while (current->next != NULL && current->next->status == STATUS_FREE) {
		current->size += METADATA_SIZE + current->next->size;
		current->next = current->next->next;
		if (current->next != NULL)
			current->next->prev = current;

		// Verific daca este spatiu suficient dupa unire
		if (current->size >= size) {
			block->size = size;
			// Daca este prea mult spatiu impart block-ul curent de memorie
			if (current->size >= size + METADATA_SIZE + 8) {
				struct block_meta *new_block = (struct block_meta *)((char *)block + METADATA_SIZE + size);

				new_block->size = current->size - size - METADATA_SIZE;
				new_block->status = STATUS_FREE;

				// Inserez noul block in lista
				new_block->prev = block;
				new_block->next = block->next;
				if (new_block->next)
					new_block->next->prev = new_block;
				block->next = new_block;
			}
			return ptr;
		}
	}

	// Verific daca pot mari block-ul folosind sbrk
	void *program_break = sbrk(0);

	if ((char *)current + METADATA_SIZE + current->size == program_break) {
		// Daca block-ul este la finalul heap-ului incerc sa il maresc
		size_t additional_size = size - current->size;

		void *new_mem = sbrk(additional_size);

		if (new_mem != (void *)-1) {
			// Block-ul a fost marit
			current->size += additional_size;
			block->size = size;

			return ptr;
		}
	}

	// Aloc un nou block de memorie si copiez datele
	void *new_ptr;

	// Pentru alocari mai mari decat MMAP_TRESHOLD folosesc mmap
	if (size >= MMAP_THRESHOLD) {
		void *mmap_ptr = mmap(NULL, size + METADATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

		DIE(mmap_ptr == MAP_FAILED, "mmap failed in realloc");
		if (mmap_ptr == MAP_FAILED)
			return NULL;

		// Initializez metadatele noului block
		struct block_meta *new_block = (struct block_meta *)mmap_ptr;

		new_block->size = size;
		new_block->status = STATUS_MAPPED;
		new_block->prev = NULL;
		new_block->next = NULL;

		new_ptr = (char *)mmap_ptr + METADATA_SIZE;
	} else {
		// Pentru alocari mai mici folosesc os_malloc
		new_ptr = os_malloc(size);
		if (new_ptr == NULL)
			return NULL;
	}

	// Copiez datele in noul block
	size_t copy_size = (block->size < size) ? block->size : size;

	memcpy(new_ptr, ptr, copy_size);

	// Eliberez vechiul block
	os_free(ptr);
	return new_ptr;
}

void os_free(void *ptr)
{
	if (ptr == NULL)
		return;

	// Extrag block-ul de metadate din pointerul la zona de memorie
	struct block_meta *block = (struct block_meta *)((char *)ptr - METADATA_SIZE);

	// Daca block-ul a fost alocat cu mmap, folosesc munmap pentru a il elibera
	if (block->status == STATUS_MAPPED) {
		int unmap_status = munmap(block, block->size + METADATA_SIZE);

		DIE(unmap_status == -1, "munmap failed");
		return;
	}

	// Statusul block-ului devine free
	block->status = STATUS_FREE;
	// Unesc block-urile de memorie adiacente dupa eliberare
	coalesce_blocks();
}
